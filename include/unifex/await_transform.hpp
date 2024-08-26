/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <unifex/coroutine.hpp>

#if UNIFEX_NO_COROUTINES
#  error "Coroutine support is required to use <unifex/await_transform.hpp>"
#endif

#include <unifex/continuations.hpp>
#include <unifex/coroutine_concepts.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>

#include <exception>
#include <optional>
#include <system_error>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _util {
enum class _state { empty, value, exception, done };

template <typename Value>
struct _expected {
  _expected() noexcept {}
  void reset_value() noexcept {
    _reset_value(std::exchange(state_, _state::empty));
  }
  ~_expected() { _reset_value(state_); }
  _state state_ = _state::empty;
  union {
    manual_lifetime<Value> value_;
    manual_lifetime<std::exception_ptr> exception_;
  };

private:
  void _reset_value(_state s) noexcept {
    switch (s) {
      case _state::value: unifex::deactivate_union_member(value_); break;
      case _state::exception:
        unifex::deactivate_union_member(exception_);
        break;
      default:;
    }
  }
};
}  // namespace _util

namespace _await_tfx {
using namespace _util;

template <typename Promise, typename Value, bool WithAsyncStackSupport>
struct _awaitable_base {
  struct type;
};

template <typename Promise, typename Sender, bool WithAsyncStackSupport>
struct _awaitable {
  struct type;
};

template <typename Promise, typename Value, bool WithAsyncStackSupport>
struct _awaitable_base<Promise, Value, WithAsyncStackSupport>::type {
  struct _rec {
  public:
    explicit _rec(
        _expected<Value>* result,
        coro::coroutine_handle<Promise> continuation) noexcept
      : result_(result)
      , continuation_(continuation) {}

    _rec(_rec&& r) noexcept
      : result_(std::exchange(r.result_, nullptr))
      , continuation_(std::move(r.continuation_)) {}

    void complete() noexcept {
      if constexpr (WithAsyncStackSupport) {
        if (auto* frame = get_async_stack_frame(continuation_.promise())) {
          detail::ScopedAsyncStackRoot root;
          root.activateFrame(*frame);
          return continuation_.resume();
        }
      }

      // run this when stacks are disabled and when the parent hasn't got one
      continuation_.resume();
    }

    template(class... Us)  //
        (requires(
            constructible_from<Value, Us...> ||
            (std::is_void_v<Value> && sizeof...(Us) == 0)))  //
        void set_value(Us&&... us) && noexcept(
            std::is_nothrow_constructible_v<Value, Us...> ||
            std::is_void_v<Value>) {
      unifex::activate_union_member(result_->value_, (Us&&)us...);
      result_->state_ = _state::value;
      complete();
    }

    void set_error(std::exception_ptr eptr) && noexcept {
      unifex::activate_union_member(result_->exception_, std::move(eptr));
      result_->state_ = _state::exception;
      complete();
    }

    void set_error(std::error_code code) && noexcept {
      std::move(*this).set_error(
          std::make_exception_ptr(std::system_error{code}));
    }

    void set_done() && noexcept {
      result_->state_ = _state::done;

      if constexpr (WithAsyncStackSupport) {
        if (auto* parentFrame =
                get_async_stack_frame(continuation_.promise())) {
          // we need a dummy frame for the waiting coroutine's unhandled_done()
          // to pop for us
          AsyncStackFrame frame;
          frame.setParentFrame(*parentFrame);

          detail::ScopedAsyncStackRoot root;
          root.activateFrame(frame);

          return continuation_.resume_done();
        }
      }

      // run this when stacks are disabled and when the parent hasn't got one
      continuation_.resume_done();
    }

    template(typename CPO)  //
        (requires is_receiver_query_cpo_v<CPO> AND
             std::is_invocable_v<CPO, const Promise&>)  //
        friend auto tag_invoke(CPO cpo, const _rec& r) noexcept(
            std::is_nothrow_invocable_v<CPO, const Promise&>)
            -> std::invoke_result_t<CPO, const Promise&> {
      const Promise& p = r.continuation_.promise();
      return std::move(cpo)(p);
    }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
    template <typename Func>
    friend void
    tag_invoke(tag_t<visit_continuations>, const _rec& r, Func&& func) {
      visit_continuations(r.continuation_.promise(), (Func&&)func);
    }
#endif

  private:
    _expected<Value>* result_;
    continuation_handle<Promise> continuation_;
  };

  bool await_ready() const noexcept { return false; }

  Value await_resume() {
    switch (result_.state_) {
      case _state::value: return std::move(result_.value_).get();
      default:
        UNIFEX_ASSERT(result_.state_ == _state::exception);
        std::rethrow_exception(result_.exception_.get());
    }
  }

protected:
  _expected<Value> result_;
};

template <typename Promise, typename Sender, bool WithAsyncStackSupport>
using _awaitable_base_t = typename _awaitable_base<
    Promise,
    sender_single_value_return_type_t<remove_cvref_t<Sender>>,
    WithAsyncStackSupport>::type;

template <typename Promise, typename Sender, bool WithAsyncStackSupport>
using _receiver_t =
    typename _awaitable_base_t<Promise, Sender, WithAsyncStackSupport>::_rec;

template <typename Promise, typename Sender, bool WithAsyncStackSupport>
struct _awaitable<Promise, Sender, WithAsyncStackSupport>::type
  : _awaitable_base_t<Promise, Sender, WithAsyncStackSupport> {
private:
  using _rec = _receiver_t<Promise, Sender, WithAsyncStackSupport>;
  connect_result_t<Sender, _rec> op_;

public:
  explicit type(Sender&& sender, coro::coroutine_handle<Promise> h) noexcept(
      is_nothrow_connectable_v<Sender, _rec>)
    : op_(unifex::connect((Sender&&)sender, _rec{&this->result_, h})) {}

  void await_suspend(coro::coroutine_handle<Promise> handle) noexcept {
    if constexpr (WithAsyncStackSupport) {
      auto* frame = get_async_stack_frame(handle.promise());
      if (frame) {
        deactivateAsyncStackFrame((*frame));
      }
    }
    unifex::start(op_);
  }
};

template <typename Promise, typename Sender, bool WithAsyncStackSupport>
using _as_awaitable =
    typename _awaitable<Promise, Sender, WithAsyncStackSupport>::type;

template <typename T, typename = void>
struct is_resumer_promise : std::false_type {};

template <typename T>
struct is_resumer_promise<T, typename T::resumer_promise_t> : std::true_type {};

template <typename T>
constexpr bool is_resumer_promise_v = is_resumer_promise<T>::value;

template <typename Promise>
struct _coro_resumer final {
  struct type;
};

template <typename Promise>
struct _coro_resumer<Promise>::type final {
  struct promise_type {
    using resumer_promise_t = void;

    static_assert(!is_resumer_promise_v<Promise>);

    promise_type(coro::coroutine_handle<Promise>& h) noexcept : handle_(h) {}

    type get_return_object() noexcept {
      return type{coro::coroutine_handle<promise_type>::from_promise(*this)};
    }

    coro::suspend_always initial_suspend() noexcept { return {}; }

    [[noreturn]] coro::suspend_always final_suspend() noexcept {
      std::terminate();
    }

    // TODO: unhandled_done()?

    [[noreturn]] void return_void() noexcept { std::terminate(); }

    [[noreturn]] void unhandled_exception() noexcept { std::terminate(); }

    struct awaiter {
      coro::coroutine_handle<Promise> h;

      bool await_ready() noexcept { return false; }

      void await_suspend(coro::coroutine_handle<>) noexcept {
        auto* frame = get_async_stack_frame(h.promise());
        if (frame) {
          detail::ScopedAsyncStackRoot root;
          root.activateFrame(*frame);

          h.resume();

          root.ensureFrameDeactivated(frame);
        } else {
          h.resume();
        }
      }

      [[noreturn]] void await_resume() noexcept { std::terminate(); }
    };

    awaiter await_transform(coro::coroutine_handle<Promise> h) noexcept {
      return awaiter{h};
    }

    template(typename CPO)                       //
        (requires is_receiver_query_cpo_v<CPO>)  //
        friend auto tag_invoke(CPO cpo, const promise_type& self) noexcept(
            is_nothrow_tag_invocable_v<CPO, const Promise&>)
            -> tag_invoke_result_t<CPO, const Promise&> {
      return tag_invoke(std::move(cpo), std::as_const(self.handle_.promise()));
    }

    continuation_handle<Promise> handle_;
  };

  type() noexcept = default;

  type(type&& other) noexcept : h_(std::exchange(other.h_, {})) {}

  ~type() {
    if (h_) {
      h_.destroy();
    }
  }

  type& operator=(type rhs) noexcept {
    std::swap(h_, rhs.h_);
    return *this;
  }

  coro::coroutine_handle<promise_type> handle() && noexcept {
    return std::exchange(h_, {});
  }

private:
  explicit type(coro::coroutine_handle<promise_type> h) noexcept : h_(h) {}

  coro::coroutine_handle<promise_type> h_;
};

template <typename Promise>
using coro_resumer = typename _coro_resumer<Promise>::type;

template <typename Promise>
coro_resumer<Promise>
resume_with_stack_root(coro::coroutine_handle<Promise> h) {
  co_await h;
}

template <typename Awaitable>
struct _awaitable_wrapper final {
  class type;
};

template <typename Awaitable>
class _awaitable_wrapper<Awaitable>::type final {
  using awaiter_t = awaiter_type_t<Awaitable>;

  Awaitable&& awaitable_;
  awaiter_t awaiter_;
  coro::coroutine_handle<> coro_;

public:
  using awaitable_wrapper_t = void;

  explicit type(Awaitable&& awaitable)
    : awaitable_(std::forward<Awaitable>(awaitable))
    , awaiter_(get_awaiter(std::forward<Awaitable>(awaitable))) {}

  type(type&& other) noexcept(std::is_nothrow_move_constructible_v<awaiter_t>)
    : awaitable_(std::move(other.awaitable_))
    , awaiter_(std::move(other.awaiter_))
    , coro_(std::exchange(other.coro_, {})) {
    // we should only be move-constructed before being awaited
    UNIFEX_ASSERT(!coro_);
  }

  ~type() {
    if (coro_) {
      coro_.destroy();
    }
  }

  bool await_ready() noexcept(noexcept(awaiter_.await_ready())) {
    return awaiter_.await_ready();
  }

  template <typename Promise>
  using resume_coro_handle_t =
      coro::coroutine_handle<typename coro_resumer<Promise>::promise_type>;

  template <typename Promise>
  using _suspend_result_t = decltype(awaiter_.await_suspend(
      resume_coro_handle_t<Promise>::from_address(nullptr)));

  template <typename Promise>
  using suspend_result_t = std::conditional_t<
      convertible_to<_suspend_result_t<Promise>, coro::coroutine_handle<>>,
      coro::coroutine_handle<>,
      _suspend_result_t<Promise>>;

  template(typename Promise)                               //
      (requires same_as<bool, suspend_result_t<Promise>>)  //
      bool await_suspend_impl(
          coro::coroutine_handle<Promise> h, AsyncStackFrame* frame) {
    auto* root = frame->getStackRoot();

    auto resumer = resume_with_stack_root(h).handle();

    // save for later destruction
    coro_ = resumer;

    // ensure that it's safe for the resumer coroutine to activate h's stack
    // frame on resumption
    deactivateAsyncStackFrame(*frame);

    if (awaiter_.await_suspend(resumer)) {
      // suspend
      return true;
    } else {
      // we're not actually suspending so undo the stack manipulation we just
      // did
      activateAsyncStackFrame(*root, *frame);

      // proactively destroy the unneeded coro_resumer
      std::exchange(coro_, {}).destroy();

      // resume the caller
      return false;
    }
  }

  template(typename Promise)                                 //
      (requires(!same_as<bool, suspend_result_t<Promise>>))  //
      suspend_result_t<Promise> await_suspend_impl(
          coro::coroutine_handle<Promise> h, AsyncStackFrame* frame) {
    auto resumer = resume_with_stack_root(h).handle();

    // save for later destruction
    coro_ = resumer;

    // ensure that it's safe for the resumer coroutine to activate h's stack
    // frame on resumption
    deactivateAsyncStackFrame(*frame);

    return awaiter_.await_suspend(resumer);
  }

  template <typename Promise>
  suspend_result_t<Promise> await_suspend(coro::coroutine_handle<Promise> h) {
    if (auto* frame = get_async_stack_frame(h.promise())) {
      return await_suspend_impl(h, frame);
    }

    using awaiter_suspend_result_t = decltype(awaiter_.await_suspend(h));

    // Note: it's technically possible for an awaitable's implementation of
    //       await_suspend() to return different types depending on its argument
    //       type. This is easily handled if the "different types" are different
    //       coroutine_handle<> types: just convert them all to
    //       coro::coroutine_handle<>; but it's a pain if the different return
    //       types mix-and-match between void, bool, and coroutine handles. If
    //       any reports ever come in that these static asserts are breaking
    //       builds, we can handle it by forcing *our* return type to always be
    //       coro::coroutine_handle<> and just map the void and bool cases to
    //       the appropriate handle, but let's avoid that complexity until it's
    //       proven necessary.
    if constexpr (same_as<void, suspend_result_t<Promise>>) {
      static_assert(same_as<void, awaiter_suspend_result_t>);
    } else if constexpr (same_as<bool, suspend_result_t<Promise>>) {
      static_assert(same_as<bool, awaiter_suspend_result_t>);
    } else {
      static_assert(
          convertible_to<awaiter_suspend_result_t, coro::coroutine_handle<>>);
    }

    return awaiter_.await_suspend(h);
  }

  auto await_resume() noexcept(noexcept(awaiter_.await_resume()))
      -> decltype(awaiter_.await_resume()) {
    return awaiter_.await_resume();
  }

  template(typename CPO)  //
      (requires same_as<tag_t<blocking>, CPO> AND
           std::is_invocable_v<CPO, const Awaitable&>)  //
      friend auto tag_invoke(CPO cpo, const type& self) noexcept(
          std::is_nothrow_invocable_v<CPO, const Awaitable&>)
          -> std::invoke_result_t<CPO, const Awaitable&> {
    return std::move(cpo)(std::as_const(self.awaitable));
  }
};

template <typename Awaitable>
using awaitable_wrapper = typename _awaitable_wrapper<Awaitable>::type;

template <typename T, typename = void>
struct is_awaitable_wrapper : std::false_type {};

template <typename T>
struct is_awaitable_wrapper<T, typename T::awaitable_wrapper_t>
  : std::true_type {};

template <typename T>
constexpr bool is_awaitable_wrapper_v = is_awaitable_wrapper<T>::value;

struct _fn {
  // Call custom implementation if present.
  template(typename Promise, typename Value)          //
      (requires tag_invocable<_fn, Promise&, Value>)  //
      auto
      operator()(Promise& promise, Value&& value) const
      noexcept(is_nothrow_tag_invocable_v<_fn, Promise&, Value>)
          -> tag_invoke_result_t<_fn, Promise&, Value> {
    static_assert(
        detail::_awaitable<tag_invoke_result_t<_fn, Promise&, Value>>,
        "The return type of a customization of unifex::await_transform() "
        "must satisfy the awaitable concept.");
    return unifex::tag_invoke(_fn{}, promise, (Value&&)value);
  }

  // Default implementation for naturally awaitable types
  template(
      typename Promise,
      typename Value,
      bool WithAsyncStackSupport = !UNIFEX_NO_ASYNC_STACKS)  //
      (requires(!tag_invocable<_fn, Promise&, Value>)
           AND detail::_awaitable<Value>)  //
      decltype(auto)
      operator()(Promise&, Value&& value) const noexcept {
    if constexpr (
        WithAsyncStackSupport &&
        !is_awaitable_wrapper_v<remove_cvref_t<Value>>) {
      return awaitable_wrapper<Value>{std::forward<Value>(value)};
    } else {
      return std::forward<Value>(value);
    }
  }

  // Default implementation for non-awaitable senders
  template(
      typename Promise,
      typename Value,
      bool WithAsyncStackSupport = !UNIFEX_NO_ASYNC_STACKS)  //
      (requires(!tag_invocable<_fn, Promise&, Value>)
           AND(!detail::_awaitable<Value>) AND unifex::sender<Value>)  //
      decltype(auto)
      operator()(Promise& promise, Value&& value) const {
    static_assert(
        unifex::sender_to<
            Value,
            _receiver_t<Promise, Value, WithAsyncStackSupport>>,
        "This sender is not awaitable in this coroutine type.");

    auto h = coro::coroutine_handle<Promise>::from_promise(promise);
    return _as_awaitable<Promise, Value, WithAsyncStackSupport>{
        (Value&&)value, h};
  }

  // Fall back to returning the argument if none of the above conditions are met
  //
  // If this case applies then value is unlikely to be awaitable and the
  // co_await expression that was routed here is unlikely to be valid. Rather
  // than cause a hard error here, return the argument unmodified and allow the
  // caller a chance to do something clever--maybe the
  // promise_type::await_transform() that's calling us has plans for this value.
  template(typename Promise, typename Value)  //
      (requires(!tag_invocable<_fn, Promise&, Value>)
           AND(!detail::_awaitable<Value>) AND(!unifex::sender<Value>))  //
      Value&&
      operator()(Promise&, Value&& value) const noexcept {
    return std::forward<Value>(value);
  }
};

}  // namespace _await_tfx

// The await_transform() customisation point allows value-types to customise
// what kind of awaitable object should be used for this type when it is used
// within a co_await expression. It is similar to 'operator co_await()' but
// works around limitations of 'operator co_await()' by providing access to
// the promise object and promise type so that different awaitable types can
// be returned depending on the awaiting context.
//
// Coroutine promise_types can implement their .await_transform() methods to
// forward to this customisation point to enable use of type customisations.
inline constexpr _await_tfx::_fn await_transform{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
