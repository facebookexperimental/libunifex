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

// // BUGBUG
// #include <cstdio>
// void printfl(char const *format, ...) {
//   va_list va;
//   va_start(va, format);
//   std::vprintf(format, va);
//   va_end(va);
//   std::putc('\n', stdout);
//   std::fflush(stdout);
// }

namespace unifex {

namespace _run_at_coroutine_exit {
inline constexpr struct _fn {
  template (typename Promise)
    (requires tag_invocable<_fn, Promise&, coro::coroutine_handle<>>)
  UNIFEX_ALWAYS_INLINE
  coro::coroutine_handle<> operator()(
      Promise& promise, coro::coroutine_handle<> action) const noexcept {
    return tag_invoke(*this, promise, (coro::coroutine_handle<>&&) action);
  }
} run_at_coroutine_exit {};
} // _run_at_coroutine_exit
using _run_at_coroutine_exit::run_at_coroutine_exit;

template <unsigned> struct undef;

template <typename... Ts>
struct [[nodiscard]] _cleanup_task {
  struct promise_type;

  struct final_awaitable {
    bool await_ready() const noexcept {
      return false;
    }
#if (defined(__clang__) && (defined(__apple_build_version__) || __clang_major__ < 12)) || \
    defined(_MSC_VER)
#if defined(_MSC_VER) 
    UNIFEX_ALWAYS_INLINE
#elif defined(__apple_build_version__) || __clang_major__ < 11
    UNIFEX_NO_INLINE
#else
    UNIFEX_ALWAYS_INLINE
#endif
    bool await_suspend(coro::coroutine_handle<promise_type> h) const noexcept {
      // printfl("%s", "_cleanup_task::final_suspend::await_suspend");
      auto continuation = h.promise().continuation_;
      h.destroy();
      continuation.resume();
      return true;
    }
#else
    coro::coroutine_handle<> await_suspend(coro::coroutine_handle<promise_type> h) const noexcept {
      //printfl("%s", "_cleanup_task::final_suspend::await_suspend");
      auto continuation = h.promise().continuation_;
      h.destroy();
      return continuation;
    }
#endif
    void await_resume() const noexcept {
    }
  };

  struct promise_type {
    template <typename Action>
    promise_type(Action&&, Ts&... ts) noexcept
      : args_(ts...) {}
    _cleanup_task get_return_object() noexcept {
      return _cleanup_task(coro::coroutine_handle<promise_type>::from_promise(*this));
    }
    coro::suspend_always initial_suspend() noexcept {
      return {};
    }
    final_awaitable final_suspend() noexcept {
      return {};
    }
    [[noreturn]] void unhandled_exception() noexcept {
      std::terminate();
    }
    // BUGBUG TODO
    [[noreturn]] coro::coroutine_handle<> unhandled_done() noexcept {
      std::terminate();
    }
    void return_void() noexcept {
    }
    template(typename Value)
      (requires callable<tag_t<unifex::await_transform>, promise_type&, Value>)
    auto await_transform(Value&& value)
        noexcept(is_nothrow_callable_v<tag_t<unifex::await_transform>, promise_type&, Value>)
        -> callable_result_t<tag_t<unifex::await_transform>, promise_type&, Value> {
      return unifex::await_transform(*this, (Value&&)value);
    }
    coro::coroutine_handle<> continuation_{};
    std::tuple<Ts&...> args_;
  };

  _cleanup_task(coro::coroutine_handle<promise_type> coro) noexcept
    : coro_(coro) {}
  _cleanup_task(_cleanup_task&& that) noexcept
    : coro_(std::exchange(that.coro_, {})) {}
  ~_cleanup_task() {
    UNIFEX_ASSERT(coro_ == nullptr);
  }

  bool await_ready() const noexcept {
    //printfl("%s", "_cleanup_task::await_ready");
    return false;
  }
  template <typename Promise>
  bool await_suspend(coro::coroutine_handle<Promise> parent) noexcept {
    //printfl("%s", "_cleanup_task::await_suspend");
    coro_.promise().continuation_ = run_at_coroutine_exit(parent.promise(), coro_);
    return false;
  }
  std::tuple<Ts&...> await_resume() noexcept {
    //printfl("%s %d", "_cleanup_task::await_resume", std::get<0>(coro_.promise().args_));
    return std::exchange(coro_, {}).promise().args_;
  }

private:
  coro::coroutine_handle<promise_type> coro_;
};

namespace _at_coroutine_exit {
  inline constexpr struct _fn {
  private:
    template (typename Action, typename... Ts)
      (requires callable<Action, Ts&...>)
    static _cleanup_task<Ts...> at_coroutine_exit(Action action, Ts... ts) {
      //printfl("%s", "at_coroutine_exit(), before cleanup action");
      co_await std::move(action)(std::move(ts)...);
      //printfl("%s", "at_coroutine_exit(), after cleanup action");
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
