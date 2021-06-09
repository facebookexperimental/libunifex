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

#include <unifex/coroutine.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/await_transform.hpp>
#include <unifex/continuations.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/inline_scheduler.hpp>
#include <unifex/any_scheduler.hpp>
#include <unifex/blocking.hpp>

#if UNIFEX_NO_COROUTINES
# error "Coroutine support is required to use this header"
#endif

#include <tuple>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _xchg_cont {
inline constexpr struct _fn {
  template (typename ParentPromise, typename ChildPromise)
    (requires tag_invocable<_fn, ParentPromise&, continuation_handle<ChildPromise>>)
  UNIFEX_ALWAYS_INLINE
  continuation_handle<> operator()(ParentPromise& parent, continuation_handle<ChildPromise> action) const noexcept {
    return tag_invoke(*this, parent, (continuation_handle<ChildPromise>&&) action);
  }
} exchange_continuation {};
} // _xchg_cont
using _xchg_cont::exchange_continuation;

struct _cleanup_promise_base {
  struct final_awaitable {
    bool await_ready() const noexcept {
      return false;
    }

    // Clang before clang-12 has a bug with coroutines that self-destruct in an
    // await_suspend that uses symmetric transfer. It appears that MSVC has the same
    // bug, while Emscripten, the WebAssembly compiler just doesn't support tail calls yet.
    // So instead of symmetric transfer, we accept the stack growth and resume
    // the continuation from within await_suspend.
#if (defined(__clang__) && (defined(__apple_build_version__) || __clang_major__ < 12)) || \
    defined(_MSC_VER) || defined(__EMSCRIPTEN__)
    template <typename CleanupPromise>
    // Apple-clang and clang-10 and prior need for await_suspend to be noinline.
    // MSVC and clang-11 can tolerate await_suspend to be inlined, so force it.
#if defined(__apple_build_version__) || (defined(__clang__) && __clang_major__ <= 10)
    UNIFEX_NO_INLINE
#else
    UNIFEX_ALWAYS_INLINE
#endif
    void await_suspend(coro::coroutine_handle<CleanupPromise> h) const noexcept {
      auto continuation = h.promise().next();
      h.destroy(); // The cleanup action has finished executing. Destroy it.
      continuation.resume();
    }
#else
    // No bugs here! OK to use symmetric transfer.
    template <typename CleanupPromise>
    coro::coroutine_handle<> await_suspend(coro::coroutine_handle<CleanupPromise> h) const noexcept {
      auto continuation = h.promise().next();
      h.destroy(); // The cleanup action has finished executing. Destroy it.
      return continuation;
    }
#endif

    void await_resume() const noexcept {
    }
  };

  coro::suspend_always initial_suspend() noexcept {
    return {};
  }

  final_awaitable final_suspend() noexcept {
    return {};
  }

  [[noreturn]] void unhandled_exception() noexcept {
    UNIFEX_ASSERT(!"An exception happened in an async cleanup action. Calling terminate...");
    std::terminate();
  }

  void return_void() noexcept {
  }

  coro::coroutine_handle<> next() const noexcept {
    return isUnhandledDone_ ? continuation_.done() : continuation_.handle();
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void
  tag_invoke(tag_t<visit_continuations>, const _cleanup_promise_base& p, Func&& func) {
    // Skip cleanup actions when visiting continuations:
    visit_continuations(p.continuation_, (Func &&) func);
  }
#endif

  friend unstoppable_token
  tag_invoke(tag_t<get_stop_token>, const _cleanup_promise_base&) noexcept {
    return unstoppable_token{};
  }

  friend any_scheduler
  tag_invoke(tag_t<get_scheduler>, const _cleanup_promise_base& p) noexcept {
    return p.sched_;
  }

  inline static constexpr inline_scheduler _default_scheduler{};
  continuation_handle<> continuation_{};
  any_scheduler sched_{_default_scheduler};
  bool isUnhandledDone_{false};
};

// The die_on_done algorithm implemented here could be implemented in terms of
// let_done, but this implementation is simpler since it doesn't instantiate
// a bunch of templates that will never be needed (no need to connect or start a
// sender returned from the done transform function).
template <typename Receiver>
struct _die_on_done_rec {
  struct type {
    Receiver rec_;
    template (typename... Ts)
      (requires receiver_of<Receiver, Ts...>)
    void set_value(Ts&&... ts) && noexcept(is_nothrow_receiver_of_v<Receiver, Ts...>) {
      unifex::set_value((Receiver&&) rec_, (Ts&&) ts...);
    }
    template (typename E)
      (requires receiver<Receiver, E>)
    void set_error(E&& e) && noexcept {
      unifex::set_error((Receiver&&) rec_, (E&&) e);
    }
    [[noreturn]] void set_done() && noexcept {
      UNIFEX_ASSERT(!"A cleanup action tried to cancel. Calling terminate...");
      std::terminate();
    }

    template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO> AND
                is_callable_v<CPO, const Receiver&>)
    friend auto tag_invoke(CPO cpo, const type& p)
        noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
        -> callable_result_t<CPO, const Receiver&> {
      return cpo(p.rec_);
    }
  };
};

template <typename Receiver>
using _die_on_done_rec_t =
    typename _die_on_done_rec<remove_cvref_t<Receiver>>::type;

template <typename Sender>
struct _die_on_done {
  struct type {
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types =
        typename sender_traits<Sender>::template value_types<Variant, Tuple>;

    template <template <typename...> class Variant>
    using error_types =
        typename sender_traits<Sender>::template error_types<Variant>;

    static constexpr bool sends_done = false;

    template (typename Receiver)
      (requires sender_to<Sender, _die_on_done_rec_t<Receiver>>)
    auto connect(Receiver&& rec) &&
        noexcept(is_nothrow_connectable_v<Sender, _die_on_done_rec_t<Receiver>>)
        -> connect_result_t<Sender, _die_on_done_rec_t<Receiver>> {
      return unifex::connect(
          (Sender&&) sender_,
          _die_on_done_rec_t<Receiver>{(Receiver&&) rec});
    }

    UNIFEX_NO_UNIQUE_ADDRESS Sender sender_;
  };
};

template <typename Sender>
using _die_on_done_t =
    typename _die_on_done<remove_cvref_t<Sender>>::type;

struct _die_on_done_fn {
  template (typename Value)
    (requires (!detail::_awaitable<Value>) AND sender<Value>)
  _die_on_done_t<Value> operator()(Value&& value) /*mutable*/
      noexcept(std::is_nothrow_constructible_v<remove_cvref_t<Value>, Value>) {
    return _die_on_done_t<Value>{(Value&&) value};
  }

  template <typename Value>
  Value&& operator()(Value&& value) const noexcept {
    return (Value&&) value;
  }
};

template <typename... Ts>
struct _cleanup_task;

template <typename... Ts>
struct _cleanup_promise : _cleanup_promise_base {
  template <typename Action>
  explicit _cleanup_promise(Action&&, Ts&... ts) noexcept
    : args_(ts...) {}

  _cleanup_task<Ts...> get_return_object() noexcept {
    return _cleanup_task<Ts...>(
        coro::coroutine_handle<_cleanup_promise>::from_promise(*this));
  }

  coro::coroutine_handle<> unhandled_done() noexcept {
    // Record that we are processing an unhandled done signal. This is checked in
    // the final_suspend of the cleanup action to know which subsequent continuation
    // to resume.
    isUnhandledDone_ = true;
    // On unhandled_done, run the cleanup action:
    return coro::coroutine_handle<_cleanup_promise>::from_promise(*this);
  }

  template <typename Value>
  decltype(auto) await_transform(Value&& value)
      noexcept(noexcept(
          unifex::await_transform(*this, _die_on_done_fn{}((Value&&) value)))) {
    return unifex::await_transform(*this, _die_on_done_fn{}((Value&&) value));
  }

  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Ts&...> args_;
};

template <typename... Ts>
struct [[nodiscard]] _cleanup_task {
  using promise_type = _cleanup_promise<Ts...>;

  explicit _cleanup_task(coro::coroutine_handle<promise_type> coro) noexcept
    : continuation_(coro) {}

  _cleanup_task(_cleanup_task&& that) noexcept
    : continuation_(std::exchange(that.continuation_, {})) {}

  ~_cleanup_task() {
    UNIFEX_ASSERT(!continuation_);
  }

  bool await_ready() const noexcept {
    return false;
  }

  template <typename Promise>
  bool await_suspend_impl_(Promise& parent) noexcept {
    continuation_.promise().continuation_ =
        exchange_continuation(parent, continuation_);
    continuation_.promise().sched_ = get_scheduler(parent);
    return false;
  }

  template <typename Promise>
  bool await_suspend(coro::coroutine_handle<Promise> parent) noexcept {
    return await_suspend_impl_(parent.promise());
  }

  std::tuple<Ts&...> await_resume() noexcept {
    return std::move(std::exchange(continuation_, {}).promise().args_);
  }

  friend constexpr auto tag_invoke(tag_t<blocking>, const _cleanup_task&) noexcept {
    return blocking_kind::always_inline;
  }

private:
  continuation_handle<promise_type> continuation_;
};

namespace _at_coroutine_exit {
  inline constexpr struct _fn {
  private:
    template <typename Action, typename... Ts>
    static _cleanup_task<Ts...> at_coroutine_exit(Action action, Ts... ts) {
      co_await std::move(action)(std::move(ts)...);
    }
  public:
    template (typename Action, typename... Ts)
      (requires callable<std::decay_t<Action>, std::decay_t<Ts>...>)
    _cleanup_task<std::decay_t<Ts>...> operator()(Action&& action, Ts&&... ts) const {
      return _fn::at_coroutine_exit((Action&&) action, (Ts&&) ts...);
    }
  } at_coroutine_exit{};
} // namespace _at_coroutine_exit

using _at_coroutine_exit::at_coroutine_exit;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
