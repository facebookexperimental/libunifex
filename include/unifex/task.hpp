/*
 * Copyright 2019-present Facebook, Inc.
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

#include <unifex/async_trace.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/std_concepts.hpp>

#if UNIFEX_NO_COROUTINES
# error "Coroutine support is required to use this header"
#endif

#include <exception>
#include <optional>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _task {

template <typename T>
struct _task {
  struct type;
};
template <typename T>
struct _task<T>::type {
  struct promise_type {
    type get_return_object() noexcept {
      return type{
          coro::coroutine_handle<promise_type>::from_promise(*this)};
    }

    coro::suspend_always initial_suspend() noexcept {
      return {};
    }

    auto final_suspend() noexcept {
      struct awaiter {
        bool await_ready() {
          return false;
        }
        auto await_suspend(
            coro::coroutine_handle<promise_type> h) noexcept {
          return h.promise().continuation_;
        }
        void await_resume() noexcept {}
      };
      return awaiter{};
    }

    void unhandled_exception() noexcept {
      reset_value();
      exception_.construct(std::current_exception());
      state_ = state::exception;
    }

    template(typename Value)
        (requires convertible_to<Value, T>)
    void return_value(Value&& value) noexcept(
        std::is_nothrow_constructible_v<T, Value>) {
      reset_value();
      value_.construct((Value &&) value);
      state_ = state::value;
    }

    promise_type() noexcept {}

    ~promise_type() {
      reset_value();
    }

    void reset_value() noexcept {
      switch (std::exchange(state_, state::empty)) {
        case state::value:
          value_.destruct();
          break;
        case state::exception:
          exception_.destruct();
          break;
        default:
          break;
      }
    }

    decltype(auto) result() {
      if (state_ == state::exception) {
        std::rethrow_exception(std::move(exception_).get());
      }
      return std::move(value_).get();
    }

    template <typename Func>
    friend void
    tag_invoke(tag_t<visit_continuations>, const promise_type& p, Func&& func) {
      if (p.info_) {
        visit_continuations(*p.info_, (Func &&) func);
      }
    }

    enum class state { empty, value, exception };

    coro::coroutine_handle<> continuation_;
    state state_ = state::empty;
    union {
      manual_lifetime<T> value_;
      manual_lifetime<std::exception_ptr> exception_;
    };
    std::optional<continuation_info> info_;
  };

  coro::coroutine_handle<promise_type> coro_;

  explicit type(coro::coroutine_handle<promise_type> h) noexcept
      : coro_(h) {}

  ~type() {
    if (coro_)
      coro_.destroy();
  }

  type(type&& t) noexcept : coro_(std::exchange(t.coro_, {})) {}

  type& operator=(type t) noexcept {
    std::swap(coro_, t.coro_);
    return *this;
  }

private:
  struct awaiter {
    coro::coroutine_handle<promise_type> coro_;
    bool await_ready() noexcept {
      return false;
    }
    template <typename OtherPromise>
    auto await_suspend(
        coro::coroutine_handle<OtherPromise> h) noexcept {
      coro_.promise().continuation_ = h;
      coro_.promise().info_.emplace(
          continuation_info::from_continuation(h.promise()));
      return coro_;
    }
    decltype(auto) await_resume() {
      return coro_.promise().result();
    }
  };

public:
  auto operator co_await() && noexcept {
    return awaiter{coro_};
  }
};
} // namespace _task

template <typename T>
using task = typename _task::_task<T>::type;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
