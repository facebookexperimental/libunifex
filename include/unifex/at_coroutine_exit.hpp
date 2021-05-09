/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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

#if UNIFEX_NO_COROUTINES
# error "Coroutine support is required to use this header"
#endif

#include <tuple>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {

struct _cleanup_promise_base;

namespace _cont {
// BUGBUG merge this with continuation_info
template <typename Promise = void>
struct continuation_handle;

template <>
struct continuation_handle<void> {
private:
  [[noreturn]] static coro::coroutine_handle<> default_done_callback(void*) noexcept {
    std::terminate();
  }

  template <typename Promise>
  static coro::coroutine_handle<> forward_unhandled_done_callback(void* p) noexcept {
    return coro::coroutine_handle<Promise>::from_address(p).promise().unhandled_done();
  }

  using done_callback_t = coro::coroutine_handle<>(*)(void*) noexcept;

  coro::coroutine_handle<> handle_{};
  done_callback_t doneCallback_ = &default_done_callback;

public:
  continuation_handle() = default;

  template (typename Promise)
    (requires (!same_as<Promise, void>))
  /*implicit*/ continuation_handle(coro::coroutine_handle<Promise> continuation) noexcept
    : handle_((coro::coroutine_handle<Promise>&&) continuation)
    , doneCallback_(&forward_unhandled_done_callback<Promise>)
  {}

  explicit operator bool() const noexcept {
    return handle_ != nullptr;
  }

  coro::coroutine_handle<> handle() const noexcept {
    return handle_;
  }

  void resume() {
    handle_.resume();
  }

  coro::coroutine_handle<> done() const noexcept {
    return doneCallback_(handle_.address());
  }
};

template <typename Promise>
struct continuation_handle {
  continuation_handle() = default;

  /*implicit*/ continuation_handle(coro::coroutine_handle<Promise> continuation) noexcept
    : self_((coro::coroutine_handle<Promise>&&) continuation)
  {}

  explicit operator bool() const noexcept {
    return !!self_;
  }

  /*implicit*/ operator continuation_handle<>() const noexcept {
    return self_;
  }

  coro::coroutine_handle<Promise> handle() const noexcept {
    return coro::coroutine_handle<Promise>::from_address(
        self_.handle().address());
  }

  void resume() {
    self_.resume();
  }

  Promise& promise() const noexcept {
    return handle().promise();
  }

  coro::coroutine_handle<> done() const noexcept {
    return self_.done();
  }

private:
  continuation_handle<> self_;
};
} // namespace _cont
using _cont::continuation_handle;

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
    // bug. So instead of symmetric transfer, we accept the stack growth and resume
    // the continuation from within await_suspend.
#if (defined(__clang__) && (defined(__apple_build_version__) || __clang_major__ < 12)) || \
    defined(_MSC_VER)
    template <typename CleanupPromise>
    // Apple-clang and clang-10 and prior need for await_suspend to be noinline.
    // MSVC and clang-11 can tolerate await_suspend to be inlined, so force it.
#if defined(_MSC_VER) || !(defined(__apple_build_version__) || __clang_major__ < 11)
    UNIFEX_ALWAYS_INLINE
#else
    UNIFEX_NO_INLINE
#endif
    bool await_suspend(coro::coroutine_handle<CleanupPromise> h) const noexcept {
      auto continuation = h.promise().next();
      h.destroy();
      continuation.resume();
      return true;
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
    std::terminate();
  }

  void return_void() noexcept {
  }

  coro::coroutine_handle<> next() const noexcept {
    return isUnhandledDone_ ? continuation_.done() : continuation_.handle();
  }

  continuation_handle<> continuation_{};
  bool isUnhandledDone_{false};
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

  template(typename Value)
    (requires callable<tag_t<unifex::await_transform>, _cleanup_promise&, Value>)
  auto await_transform(Value&& value)
      noexcept(is_nothrow_callable_v<tag_t<unifex::await_transform>, _cleanup_promise&, Value>)
      -> callable_result_t<tag_t<unifex::await_transform>, _cleanup_promise&, Value> {
    return unifex::await_transform(*this, (Value&&) value);
  }

  std::tuple<Ts&...> args_;
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
  bool await_suspend(coro::coroutine_handle<Promise> parent) noexcept {
    continuation_.promise().continuation_ =
        exchange_continuation(parent.promise(), continuation_);
    return false;
  }

  std::tuple<Ts&...> await_resume() noexcept {
    return std::exchange(continuation_, {}).promise().args_;
  }

private:
  continuation_handle<promise_type> continuation_;
};

namespace _at_coroutine_exit {
  inline constexpr struct _fn {
  private:
    template (typename Action, typename... Ts)
      (requires callable<Action, Ts&...>)
    static _cleanup_task<Ts...> at_coroutine_exit(Action action, Ts... ts) {
      co_await std::move(action)(std::move(ts)...);
    }
  public:
    template (typename Action, typename... Ts)
      (requires callable<std::decay_t<Action>, std::decay_t<Ts>&...>)
    _cleanup_task<Ts...> operator()(Action&& action, Ts&&... ts) const {
      return _fn::at_coroutine_exit((Action&&) action, (Ts&&) ts...);
    }
  } at_coroutine_exit{};
} // namespace _at_coroutine_exit

using _at_coroutine_exit::at_coroutine_exit;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
