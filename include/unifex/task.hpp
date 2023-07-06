/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/any_scheduler.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/await_transform.hpp>
#include <unifex/blocking.hpp>
#include <unifex/connect_awaitable.hpp>
#include <unifex/continuations.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/coroutine_concepts.hpp>
#include <unifex/defer.hpp>
#include <unifex/finally.hpp>
#include <unifex/inline_scheduler.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/invoke.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/on.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/then.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/unstoppable.hpp>
#include <unifex/with_scheduler_affinity.hpp>

#if UNIFEX_NO_COROUTINES
#  error "Coroutine support is required to use this header"
#endif

#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _task {
using namespace _util;

/**
 * An RAII owner of a coroutine_handle<>.
 */
struct coro_holder {
  explicit coro_holder(coro::coroutine_handle<> h) noexcept
    : coro_(std::move(h)) {}

  coro_holder(coro_holder&& other) noexcept
    : coro_(std::exchange(other.coro_, {})) {}

  ~coro_holder() {
    if (coro_) {
      coro_.destroy();
    }
  }

  coro_holder& operator=(coro_holder rhs) noexcept {
    std::swap(coro_, rhs.coro_);
    return *this;
  }

protected:
  coro::coroutine_handle<> coro_;
};

/**
 * An RAII owner of a coroutine_handle<> that steals a low bit for an extra
 * flag.
 */
struct tagged_coro_holder {
  explicit tagged_coro_holder(coro::coroutine_handle<> h) noexcept
    : coro_(reinterpret_cast<std::uintptr_t>(h.address())) {
    UNIFEX_ASSERT(coro_);
  }

  tagged_coro_holder(tagged_coro_holder&& other) noexcept
    : coro_(std::exchange(other.coro_, 0)) {
    UNIFEX_ASSERT(coro_ && ((coro_ & 1u) == 0u));
  }

  ~tagged_coro_holder() {
    static constexpr std::uintptr_t mask = ~(std::uintptr_t{1u});

    if ((coro_ & mask) != 0u) {
      auto address = reinterpret_cast<void*>(coro_ & mask);
      coro::coroutine_handle<>::from_address(address).destroy();
    }
  }

protected:
  // Stored as an integer so we can use the low bit as a dirty bit
  std::uintptr_t coro_;
};

template <typename T, bool nothrow>
struct _task {
  /**
   * The "public facing" task<> type.
   */
  struct [[nodiscard]] type;
};

template <typename T, bool nothrow>
struct _sa_task final {
  /**
   * A "scheduler-affine" task that's used as an implementation detail to mark a
   * given task<> as running with the scheduler-affinity invariant maintained by
   * the caller.
   */
  struct [[nodiscard]] type;
};

template <typename T>
struct _sr_thunk_task final {
  /**
   * A special coroutine type that gets interposed between a task<> and its
   * caller to guarantee that stop requests are delivered to the task<> on the
   * task<>'s scheduler.
   *
   * "sr thunk" refers to "stop request thunk".
   */
  struct [[nodiscard]] type;
};

/**
 * A base class for both task<> and sr_thunk_task<>'s promises' final-suspend
 * awaitable.
 */
struct _final_suspend_awaiter_base {
  bool await_ready() noexcept { return false; }

  void await_resume() noexcept {}

  // TODO: we need to address always-inline awaitables
  friend constexpr auto tag_invoke(
      tag_t<unifex::blocking>, const _final_suspend_awaiter_base&) noexcept {
    return blocking_kind::always_inline;
  }
};

/**
 * Common behaviour and data for task<> and sr_thunk_task<>'s promise types.
 */
struct _promise_base {
  /**
   * Our coroutine types are lazy so initial_suspend() returns suspend_always.
   */
  coro::suspend_always initial_suspend() noexcept { return {}; }

  friend any_scheduler
  tag_invoke(tag_t<get_scheduler>, const _promise_base& p) noexcept {
    return p.sched_;
  }

  friend continuation_handle<> tag_invoke(
      const tag_t<exchange_continuation>&,
      _promise_base& p,
      continuation_handle<> action) noexcept {
    return std::exchange(p.continuation_, std::move(action));
  }

#ifdef UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void
  tag_invoke(tag_t<visit_continuations>, const _promise_base& p, Func&& func) {
    visit_continuations(p.continuation_, static_cast<Func&&>(func));
  }
#endif

  inline static constexpr inline_scheduler _default_scheduler{};

  // the coroutine awaiting our completion
  continuation_handle<> continuation_;
  // the scheduler we run on
  any_scheduler sched_{_default_scheduler};
  // a stop token from our receiver, possibly adapted through an adapter
  inplace_stop_token stoken_;
};

/**
 * The parts of a task<T>'s promise that don't depend on T.
 */
struct _task_promise_base : _promise_base {
  // the implementation of the magic of co_await schedule(s); this is to be
  // ripped out and replaced with something more explicit
  void transform_schedule_sender_impl_(any_scheduler newSched);

  coro::coroutine_handle<> unhandled_done() noexcept {
    return continuation_.done();
  }

  void register_stop_callback() noexcept {}

  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const _task_promise_base& p) noexcept {
    return p.stoken_;
  }

  // has this task<> been rescheduled onto a new scheduler?
  bool rescheduled_ = false;
};

template <typename T, bool nothrow>
struct _result_and_unhandled_exception final {
  /**
   * Storage for a task<T> or sr_thunk_task<T>'s result, plus handling for
   * unhandled exceptions.
   *
   * This is used to share the implementation of result handling between
   * type-specific promise types.
   */
  struct type { //parametize on noexcept, noexcept terminates in body
    void unhandled_exception() noexcept { // will be invoked in the catch block of the coroutine
      if constexpr (nothrow) {
        std::terminate();
      } else {
        result_.reset_value();
        unifex::activate_union_member(
            result_.exception_, std::current_exception());
        result_.state_ = _state::exception;
      }
    }

    decltype(auto) result() noexcept(nothrow) {
      if constexpr (nothrow) {
        return std::move(result_).get();
      } else {
        if (result_.state_ == _state::exception) {
          std::rethrow_exception(std::move(result_.exception_).get());
        }
        return std::move(result_.value_).get();
      }
    }

    template <typename... Args>
    // todo: consider if this should be nothrow or not
    void set_value(Args&&... values) {
      if constexpr (nothrow) {
        result_.construct(static_cast<Args&&>(values)...);
      } else {
        this->result_.reset_value();
        unifex::activate_union_member(
            this->result_.value_, static_cast<Args&&>(values)...);
        this->result_.state_ = _state::value;
      }
    }

    std::conditional_t<nothrow, manual_lifetime<T>, _expected<T>> result_;
  };
};

template <typename T, bool nothrow>
struct _return_value_or_void {
  /**
   * Provides a type-specific return_value() method to meet a promise type's
   * requirements.
   */
   // todo: consider if this should be nothrow or not
  struct type : _result_and_unhandled_exception<T, nothrow>::type {
    template(typename Value = T)                                              //
        (requires convertible_to<Value, T> AND constructible_from<T, Value>)  //
        void return_value(Value&& value) noexcept(
            std::is_nothrow_constructible_v<T, Value>) {
      this->set_value(static_cast<Value&&>(value));
    }
  };
};

template <bool nothrow>
struct _return_value_or_void<void, nothrow> {
  /**
   * Provides a return_void() method to meet a promise type's requirements.
   */
  struct type : _result_and_unhandled_exception<void, nothrow>::type {
    void return_void() noexcept {
      this->set_value();
    }
  };
};

/**
 * A marker type that task<> inherits from.  I'd like to deprecate and remove
 * this but, Hyrum's law.
 */
struct _task_base {};

template <typename T, bool nothrow>
struct _promise final {
  /**
   * The promise_type for task<>; inherits _task_promise_base for common
   * functionality, and _return_value_or_void<T> for result handling.
   */
  struct type final
    : _task_promise_base
    , _return_value_or_void<T, nothrow>::type {
    using result_type = T;

    typename _task<T, nothrow>::type get_return_object() noexcept {
      return typename _task<T, nothrow>::type{
          coro::coroutine_handle<type>::from_promise(*this)};
    }

    auto final_suspend() noexcept {
      struct awaiter final : _final_suspend_awaiter_base {
#if (defined(_MSC_VER) && !defined(__clang__)) || defined(__EMSCRIPTEN__)
        // MSVC doesn't seem to like symmetric transfer in this final awaiter
        // and the Emscripten (WebAssembly) compiler doesn't support tail-calls
        void await_suspend(coro::coroutine_handle<type> h) noexcept {
          return h.promise().continuation_.handle().resume();
        }
#else
        auto await_suspend(coro::coroutine_handle<type> h) noexcept {
          return h.promise().continuation_.handle();
        }
#endif
      };
      return awaiter{};
    }

    template <typename Value>
    // todo: consider if this should be nothrow or not
    // NOTE: Magic rescheduling is not currently supported by nothrow tasks
    decltype(auto) await_transform(Value&& value) {
      if constexpr (is_sender_for_v<remove_cvref_t<Value>, schedule>) {
        static_assert(!nothrow, "Magic rescheduling isn't supported by no-throw tasks");
        // TODO: rip this out and replace it with something more explicit

        // If we are co_await'ing a sender that is the result of calling
        // schedule, do something special
        return transform_schedule_sender_(static_cast<Value&&>(value));
      } else if constexpr (unifex::sender<Value>) {
        return unifex::await_transform(
            *this,
            with_scheduler_affinity(static_cast<Value&&>(value), this->sched_));
      } else if constexpr (
          tag_invocable<tag_t<unifex::await_transform>, type&, Value> ||
          detail::_awaitable<Value>) {
        // Either await_transform has been customized or Value is an awaitable.
        // Either way, we can dispatch to the await_transform CPO, then insert a
        // transition back to the correct execution context if necessary.
        return with_scheduler_affinity(
            *this,
            unifex::await_transform(*this, static_cast<Value&&>(value)),
            this->sched_);
      } else {
        // Otherwise, we don't know how to await this type. Just return it and
        // let the compiler issue a diagnostic.
        return (Value &&) value;
      }
    }

    // co_await schedule(sched) is magical. It does the following:
    // - transitions execution context
    // - updates the coroutine's current scheduler
    // - schedules an async cleanup action that transitions back to the correct
    //   context at the end of the coroutine (if one has not already been
    //   scheduled).
    template <typename ScheduleSender>
    decltype(auto) transform_schedule_sender_(ScheduleSender&& snd) {
      // This sender is a scheduler provider. Get the scheduler. This
      // get_scheduler call returns a reference to the scheduler stored within
      // snd, which is an object whose lifetime spans a suspend point. So it's
      // ok to build an any_scheduler_ref from it:
      transform_schedule_sender_impl_(get_scheduler(snd));

      // Return the inner sender, appropriately wrapped in an awaitable:
      return unifex::await_transform(*this, std::forward<ScheduleSender>(snd).base());
    }
  };
};

struct _sr_thunk_promise_base : _promise_base {
  coro::coroutine_handle<> unhandled_done() noexcept {
    callback_.destruct();

    if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      return continuation_.done();
    } else {
      return coro::noop_coroutine();
    }
  }

  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const _sr_thunk_promise_base& p) noexcept {
    return p.stopSource_.get_token();
  }

  mutable inplace_stop_source stopSource_;

  struct deferred_stop_request final {
    _sr_thunk_promise_base* self;

    auto operator()() noexcept -> decltype(unstoppable(on(
        self->sched_,
        just(&self->stopSource_) | then(&inplace_stop_source::request_stop)))) {
      return unstoppable(on(
          self->sched_,
          just(&self->stopSource_) | then(&inplace_stop_source::request_stop)));
    }
  };

  using sender_t =
      decltype(unifex::defer(UNIFEX_DECLVAL(deferred_stop_request)));

  struct receiver_t {
    _sr_thunk_promise_base* self;

    void set_value(bool) noexcept {
      if (self->refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        self->continuation_.handle().resume();
      }
    }
    void set_error(std::exception_ptr) noexcept { std::terminate(); }
    void set_done() noexcept { std::terminate(); }
  };

  using op_t = connect_result_t<sender_t, receiver_t>;

  op_t stopOperation_{
      connect(unifex::defer(deferred_stop_request{this}), receiver_t{this})};

  struct stop_callback {
    _sr_thunk_promise_base* self;

    void operator()() noexcept {
      if (self->refCount_.fetch_add(1, std::memory_order_relaxed) == 0) {
        return;
      }

      unifex::start(self->stopOperation_);
    }
  };

  using stop_callback_t =
      typename inplace_stop_token::callback_type<stop_callback>;

  manual_lifetime<stop_callback_t> callback_;

  std::atomic<uint8_t> refCount_{1};

  void register_stop_callback() noexcept {
    callback_.construct(stoken_, stop_callback{this});
  }
};

//TODO: determine if this should also be nothrow
template <typename T>
struct _sr_thunk_promise final {
  /**
   * The promise_type for an sr_thunk_task<T>.
   *
   * This type has two main responsibilities:
   *  - register a stop callback on our receiver's stop token that, when
   *    invoked, executes an async operation that forwards the request to the
   *    nested stop source on the correct scheduler; and
   *  - ensure that, if the async stop request is ever started, we wait for
   *    *both* the async stop request *and* the nested operation to complete
   *    before continuing our continuation.
   *
   * The async stop request delivery is handled in _sr_thunk_promise_base (our
   * base class), and our final-awaiter handles coordinating who continues our
   * continuation.
   */
  struct type final
    : _sr_thunk_promise_base
    , _return_value_or_void<T, false>::type {
    using result_type = T;

    typename _sr_thunk_task<T>::type get_return_object() noexcept {
      return typename _sr_thunk_task<T>::type{
          coro::coroutine_handle<type>::from_promise(*this)};
    }

    auto final_suspend() noexcept {
      struct awaiter final : _final_suspend_awaiter_base {
#if (defined(_MSC_VER) && !defined(__clang__)) || defined(__EMSCRIPTEN__)
        // MSVC doesn't seem to like symmetric transfer in this final awaiter
        // and the Emscripten (WebAssembly) compiler doesn't support tail-calls
        void await_suspend(coro::coroutine_handle<type> h) noexcept {
          auto& p = h.promise();

          p.callback_.destruct();

          // if we're last to complete, continue our continuation; otherwise do
          // nothing and wait for the async stop request to do it
          if (p.refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            return h.promise().continuation_.handle().resume();
          }
          // nothing
        }
#else
        coro::coroutine_handle<>
        await_suspend(coro::coroutine_handle<type> h) noexcept {
          auto& p = h.promise();

          p.callback_.destruct();

          // if we're last to complete, continue our continuation; otherwise do
          // nothing and wait for the async stop request to do it
          if (p.refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            return h.promise().continuation_.handle();
          } else {
            return coro::noop_coroutine();
          }
        }
#endif
      };

      return awaiter{};
    }

    template <typename Value>
    decltype(auto) await_transform(Value&& value) {
      return unifex::await_transform(*this, static_cast<Value&&>(value));
    }
  };
};

template <typename ThisPromise, typename OtherPromise>
struct _awaiter final {
  /**
   * An awaitable type that knows how to await a task<>, sa_task<>, or
   * sr_thunk_task<> from a coroutine whose promise_type is OtherPromise.
   *
   * We inherit tagged_coro_holder to be able to distinguish whether or not the
   * awaited task has been started, and thus whether we need to clean it up in
   * our destructor.
   */
  struct type : tagged_coro_holder {
    using result_type = typename ThisPromise::result_type;

    explicit type(coro::coroutine_handle<> coro) noexcept
      : tagged_coro_holder(coro) {}

    // The move constructor is only ever called /before/ the awaitable is
    // awaited. In those cases, the other fields have not been initialized yet
    // and so do not need to be moved.
    type(type&& other) noexcept : tagged_coro_holder(std::move(other)) {}

    ~type() {
      if (coro_ & 1u) {
        if constexpr (needs_stop_token_t::value)
          stopTokenAdapter_.unsubscribe();
        if constexpr (needs_scheduler_t::value)
          sched_.destruct();
      }
    }

    bool await_ready() noexcept { return false; }

    coro::coroutine_handle<ThisPromise>
    await_suspend(coro::coroutine_handle<OtherPromise> h) noexcept {
      UNIFEX_ASSERT(coro_ && ((coro_ & 1u) == 0u));
      auto thisCoro =
          coro::coroutine_handle<ThisPromise>::from_address((void*)coro_);
      ++coro_;  // mark the awaiter as needing cleanup
      auto& promise = thisCoro.promise();
      promise.continuation_ = h;
      if constexpr (needs_scheduler_t::value) {
        sched_.construct(get_scheduler(h.promise()));
        promise.sched_ = sched_.get();
      } else {
        promise.sched_ = get_scheduler(h.promise());
      }
      if constexpr (needs_stop_token_t::value) {
        promise.stoken_ =
            stopTokenAdapter_.subscribe(get_stop_token(h.promise()));
      } else {
        promise.stoken_ = get_stop_token(h.promise());
      }

      promise.register_stop_callback();

      return thisCoro;
    }

    result_type await_resume() noexcept(noexcept(UNIFEX_DECLVAL(ThisPromise&).result())) {
      if constexpr (needs_stop_token_t::value)
        stopTokenAdapter_.unsubscribe();
      if constexpr (needs_scheduler_t::value)
        sched_.destruct();
      auto thisCoro = coro::coroutine_handle<ThisPromise>::from_address(
          (void*)std::exchange(--coro_, 0));
      coro_holder destroyOnExit{thisCoro};
      return thisCoro.promise().result();
    }

  private:
    using scheduler_t = remove_cvref_t<get_scheduler_result_t<OtherPromise&>>;
    using stop_token_t = remove_cvref_t<stop_token_type_t<OtherPromise>>;
    using needs_scheduler_t =
        std::bool_constant<!same_as<scheduler_t, any_scheduler>>;
    using needs_stop_token_t =
        std::bool_constant<!same_as<stop_token_t, inplace_stop_token>>;

    // Only store the scheduler and the stop_token in the awaiter if we need to
    // type erase them. Otherwise, these members are "empty" and should take up
    // no space because of the [[no_unique_address]] attribute. Note: for the
    // compiler to fold the members away, they must have different types. Hence,
    // the slightly odd-looking template parameter to the empty struct.
    UNIFEX_NO_UNIQUE_ADDRESS
    conditional_t<
        needs_scheduler_t::value,
        manual_lifetime<scheduler_t>,
        detail::_empty<0>>
        sched_;
    UNIFEX_NO_UNIQUE_ADDRESS
    conditional_t<
        needs_stop_token_t::value,
        inplace_stop_token_adapter<stop_token_t>,
        detail::_empty<1>>
        stopTokenAdapter_;
  };
};

/**
 * The coroutine type that ensures stop requests are delivered to nested task<>s
 * on the right scheduler.
 */
template <typename T>
struct _sr_thunk_task<T>::type final : coro_holder {
  using promise_type = typename _sr_thunk_promise<T>::type;
  friend promise_type;

private:
  template <typename OtherPromise>
  using awaiter = typename _awaiter<promise_type, OtherPromise>::type;

  explicit type(coro::coroutine_handle<promise_type> h) noexcept
    : coro_holder(h) {}

  template <typename Promise>
  friend awaiter<Promise>
  tag_invoke(tag_t<unifex::await_transform>, Promise&, type&& t) noexcept {
    return awaiter<Promise>{std::exchange(t.coro_, {})};
  }
};

/**
 * Await the given sa_task<> in a context that will deliver stop requests from
 * the receiver on the expected scheduler.
 */
template <typename T, bool nothrow>
typename _sr_thunk_task<T>::type
inject_stop_request_thunk(typename _sa_task<T, nothrow>::type awaitable) {
  // I wonder if we could do better than hopping through this extra coroutine
  co_return co_await std::move(awaitable);
}

template <typename T, bool nothrow>
struct _task<T, nothrow>::type
  : _task_base
  , coro_holder {
  using promise_type = typename _promise<T, nothrow>::type;
  friend promise_type;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types =
      Variant<typename std::
                  conditional_t<std::is_void_v<T>, type_list<>, type_list<T>>::
                      template apply<Tuple>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  // we can't tell whether the coroutine has any suspend points beyond the
  // initial one and, even if we could, we wouldn't know if any of those suspend
  // points are async
  static constexpr blocking_kind blocking = blocking_kind::maybe;

  static constexpr bool is_always_scheduler_affine = true;

  type(type&& t) noexcept = default;

  type& operator=(type&& t) noexcept = default;

  template <typename Fn, typename... Args>
  friend type
  tag_invoke(tag_t<co_invoke>, type_identity<type>, Fn fn, Args... args) noexcept(false) /* even if nothrow is true, ramp of a coroutine can still throw */ {
    co_return co_await std::invoke((Fn &&) fn, (Args &&) args...);
  }

private:
  explicit type(coro::coroutine_handle<promise_type> h) noexcept
    : coro_holder(h) {}

  template <typename Promise>
  friend auto tag_invoke(tag_t<unifex::await_transform>, Promise& p, type&& t) noexcept(false) /* calls inject_stop_request_thunk which might throw */ {
    // we don't know whether our consumer will enforce the scheduler-affinity
    // invariants so we need to ensure that stop requests are delivered on the
    // right scheduler
    return unifex::await_transform(
        p, inject_stop_request_thunk<T, nothrow>(std::move(t)));
  }

  template <typename Receiver>
  friend auto tag_invoke(tag_t<unifex::connect>, type&& t, Receiver&& r) noexcept(false) /* will ultimately call a coroutine that might throw */ {
    using stoken_t = stop_token_type_t<Receiver>;

    if constexpr (is_stop_never_possible_v<stoken_t>) {
      // NOTE: we *don't* need to worry about stop requests if the receiver's
      //       stop token can't make such requests!
      using sa_task = typename _sa_task<T, nothrow>::type;

      return connect(sa_task{std::move(t)}, static_cast<Receiver&&>(r));
    } else {
      // connect_awaitable will get the awaitable to connect by invoking
      // await_transform so we can guarantee stop requests are delivered
      // on the right scheduler by relying on await_transform to do that
      return connect_awaitable(std::move(t), static_cast<Receiver&&>(r));
    }
  }

  template <typename Scheduler>
  friend typename _sa_task<T, nothrow>::type tag_invoke(
      tag_t<with_scheduler_affinity>, type&& task, Scheduler&&) noexcept {
    return {std::move(task)};
  }
};

/**
 * A "sheduler-affine" task<>; an sa_task<> is the same as a task<> except that
 * it expects its consumer to maintain the scheduler-affinity invariant and so
 * it can avoid the overhead required to establish the invariant itself.
 *
 * The main difference is that await_transform doesn't indirect through
 * inject_stop_request_thunk.
 */
template <typename T, bool nothrow>
struct _sa_task<T, nothrow>::type final : public _task<T, nothrow>::type {
  using base = typename _task<T, nothrow>::type;

  type(base&& t) noexcept : base(std::move(t)) {}

  template <typename OtherPromise>
  using awaiter =
      typename _awaiter<typename base::promise_type, OtherPromise>::type;

  // given that we're awaited in a scheduler-affine context, we are ourselves
  // scheduler-affine
  static constexpr bool is_always_scheduler_affine = true;

  template <typename Promise>
  friend awaiter<Promise>
  tag_invoke(tag_t<unifex::await_transform>, Promise&, type&& t) noexcept {
    return awaiter<Promise>{std::exchange(t.coro_, {})};
  }

  template <typename Receiver>
  friend auto
  tag_invoke(tag_t<unifex::connect>, type&& t, Receiver&& r) noexcept(false) /* ultimately calls a coroutine which may throw */ {
    return connect_awaitable(std::move(t), static_cast<Receiver&&>(r));
  }
};

}  // namespace _task

template <typename T>
using task = typename _task::_task<T, false>::type;

template <typename T>
using nothrow_task = typename _task::_task<T, true>::type;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
