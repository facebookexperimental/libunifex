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

#include <unifex/config.hpp>

#if !UNIFEX_NO_COROUTINES

#  include <unifex/coroutine.hpp>
#  include <unifex/std_concepts.hpp>

#  include <exception>
#  include <type_traits>
#  include <utility>

#  include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _unhandled_done {

struct _func_awaiter_base {
  constexpr bool await_ready() noexcept { return false; }

  [[noreturn]] void await_resume() noexcept { std::terminate(); }
};

template <typename Func>
struct _func_awaiter final : _func_awaiter_base {
  static_assert(!std::is_reference_v<Func>);
  static_assert(std::is_nothrow_invocable_v<Func>);
  static_assert(
      convertible_to<std::invoke_result_t<Func>, coro::coroutine_handle<>>);

  Func&& func_;

  coro::coroutine_handle<> await_suspend(coro::coroutine_handle<>) noexcept {
    return std::move(func_)();
  }
};

struct _done_coro {
  struct promise_type {
    _done_coro get_return_object() noexcept {
      return _done_coro{
          coro::coroutine_handle<promise_type>::from_promise(*this)};
    }

    coro::suspend_always initial_suspend() noexcept { return {}; }

    [[noreturn]] coro::suspend_always final_suspend() noexcept {
      std::terminate();
    }

    [[noreturn]] void unhandled_exception() noexcept { std::terminate(); }

    void return_void() noexcept {}

    template <typename Func>
    _func_awaiter<Func> await_transform(Func&& func) noexcept {
      return {{}, std::move(func)};
    }

    template <typename Func>
    void await_transform(Func&) = delete;
  };

  _done_coro() noexcept = default;

  _done_coro(_done_coro&& other) noexcept
    : handle_{std::exchange(other.handle_, {})} {}

  ~_done_coro() {
    if (handle_) {
      handle_.destroy();
    }
  }

  _done_coro& operator=(_done_coro other) noexcept {
    std::swap(handle_, other.handle_);
    return *this;
  }

  coro::coroutine_handle<> handle() const noexcept { return handle_; }

private:
  explicit _done_coro(coro::coroutine_handle<promise_type> h) noexcept
    : handle_{h} {}

  coro::coroutine_handle<promise_type> handle_{};
};

template(typename Func)  //
    (requires std::is_nothrow_invocable_v<Func> AND convertible_to<
        std::invoke_result_t<Func>,
        coro::coroutine_handle<>>)  //
    _done_coro unhandled_done(Func func) {
  co_await std::move(func);
}

struct _fn final {
  template(typename Func)  //
      (requires std::is_nothrow_invocable_v<remove_cvref_t<Func>> AND
           convertible_to<
               std::invoke_result_t<remove_cvref_t<Func>>,
               coro::coroutine_handle<>>)  //
      _done_coro
      operator()(Func&& func) const {
    return unhandled_done(std::forward<Func>(func));
  }

  template(typename Func)  //
      (requires std::is_nothrow_invocable_v<remove_cvref_t<Func>> AND
           same_as<std::invoke_result_t<remove_cvref_t<Func>>, void>)  //
      _done_coro
      operator()(Func&& func) const {
    return unhandled_done([func = std::forward<Func>(func)]() noexcept(
                              std::is_nothrow_invocable_v<Func>) {
      std::move(func)();
      return coro::noop_coroutine();
    });
  }
};

}  // namespace _unhandled_done

/**
 * unhandled_done() takes a callable and returns a done_coro that owns a
 * suspended coroutine that, when resumed, will invoke the callable with no
 * arguments and then resume the coroutine_handle<> it returns. If the callable
 * returns void, resuming the returned coroutine will invoke the callable and
 * then suspend.
 *
 * The done_coro returned from unhandled_done() is intended to be used to
 * implement a coroutine promise type's unhandled_done() function like so:
 *
 *   struct promise_type {
 *     promise_type()
 *      : doneCoro_(unifex::unhandled_done([] { ... })) {}
 *
 *     coroutine_handled<> unhandled_done() noexcept {
 *       return doneCoro_.handle();
 *     }
 *
 *     // the rest of a promise implementation...
 *
 *     unifex::done_coro doneCoro_;
 *   };
 *
 * A coroutine's promise's unhandled_done() will be invoked by a *child*
 * awaitable to signal that it completed with done (rather than resuming the
 * parent coroutine directly, which triggers invocation of the child's
 * await_resume(), to signal either value or error completion). The coroutine
 * returned from a promise's unhandled_done() is responsible for cleaning up the
 * child that completed with done before performing the parent coroutine's "on
 * done" duties.
 */
constexpr _unhandled_done::_fn unhandled_done{};

/**
 * The return type of unhandled_done().
 */
using done_coro = _unhandled_done::_done_coro;

}  // namespace unifex

#  include <unifex/detail/epilogue.hpp>

#endif  // !UNIFEX_NO_COROUTINES
