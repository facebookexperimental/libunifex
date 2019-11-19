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

#include <unifex/manual_lifetime.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/coroutine.hpp>

#if UNIFEX_NO_COROUTINES
#error                                                                         \
    "C++20 coroutine support is required to use <unifex/sender_awaitable.hpp>"
#endif

#include <cassert>
#include <exception>
#include <optional>

namespace unifex {

template<typename Sender, typename Value>
struct sender_awaiter {

  struct coroutine_receiver {
    sender_awaiter& awaiter_;

    template<typename... Values>
    void value(Values&&... values) && noexcept {
      if constexpr (std::is_nothrow_constructible_v<Value, Values...>) {
        awaiter_.value_.construct((Values&&)values...);
        awaiter_.state_ = state::value;
      } else {
        try {
            awaiter_.value_.construct((Values&&)values...);
            awaiter_.state_ = state::value;
        } catch (...) {
            awaiter_.ex_.construct(std::current_exception());
            awaiter_.state_ = state::error;
        }
      }
      awaiter_.continuation_.resume();
    }

    template<typename Error>
    void error(Error&& error) && noexcept {
      std::move(*this).error(std::make_exception_ptr((Error&&)error));
    }

    void error(std::exception_ptr ex) && noexcept {
      awaiter_.ex_.construct(std::move(ex));
      awaiter_.state_ = state::error;
      awaiter_.continuation_.resume();
    }

    void done() && noexcept {
      awaiter_.state_ = state::done;
      awaiter_.continuation_.resume();
    }

    template <typename Func>
    friend auto tag_invoke(
        tag_t<visit_continuations>,
        const coroutine_receiver& r,
        Func&& func) {
      if (r.awaiter_.info_) {
        visit_continuations(*r.awaiter_.info_, (Func &&) func);
      }
    }
  };

  explicit sender_awaiter(Sender&& sender)
  : op_(connect(
      static_cast<Sender&&>(sender),
      coroutine_receiver{*this}))
  {}

  ~sender_awaiter() {
    switch (state_) {
      case state::value: value_.destruct(); break;
      case state::error: ex_.destruct(); break;
      default: break;
    }
  }

  bool await_ready() noexcept { return false; }

  template <typename Promise>
  void await_suspend(coro::coroutine_handle<Promise> h) noexcept {
    continuation_ = h;
    if constexpr (!std::is_void_v<Promise>) {
      info_.emplace(continuation_info::from_continuation(h.promise()));
    }
    start(op_);
  }

  auto await_resume()
      noexcept(
        is_sender_nofail_v<std::remove_reference_t<Sender>> &&
        std::is_nothrow_move_constructible_v<Value> &&
        Sender::template value_types<
          std::conjunction,
          is_nothrow_constructible_from<Value>::template apply>::value) {
    if constexpr (std::is_void_v<Value>) {
        if (state_ == state::done) {
            return;
        }
    } else {
        if (state_ == state::value) {
            return std::optional<Value>{std::move(value_).get()};
        } else if (state_ == state::done) {
            return std::optional<Value>{std::nullopt};
        }
    }
    assert(state_ == state::error);
    std::rethrow_exception(std::move(ex_.get()));
  }

private:

  enum class state {
    empty,
    done,
    value,
    error
  };

  state state_ = state::empty;
  operation_t<Sender, coroutine_receiver> op_;
  coro::coroutine_handle<> continuation_;
  union {
    manual_lifetime<Value> value_;
    manual_lifetime<std::exception_ptr> ex_;
  };
  std::optional<continuation_info> info_;
};

template<
  typename Sender,
  typename Result = single_value_result_t<std::remove_reference_t<Sender>>>
auto operator co_await(Sender&& sender) {
  return sender_awaiter<Sender, Result>{(Sender&&)sender};
}

template<
    typename Sender,
    std::enable_if_t<
      std::remove_reference_t<Sender>::template value_types<
        is_empty_list, is_empty_list>::value,
      int> = 0>
auto operator co_await(Sender&& sender) {
  return sender_awaiter<Sender, void>{(Sender&&)sender};
}

}
