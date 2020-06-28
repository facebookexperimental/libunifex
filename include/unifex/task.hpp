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
#include <unifex/coroutine_concepts.hpp>
#include <unifex/std_concepts.hpp>

#if UNIFEX_NO_COROUTINES
# error "Coroutine support is required to use this header"
#endif

#include <exception>
#include <optional>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _task {

template <typename... Types>
using _is_single_valued_tuple =
    std::bool_constant<1 >= sizeof...(Types)>;

template <typename... Types>
using _is_single_valued_variant =
    std::bool_constant<sizeof...(Types) == 1 && (Types::value &&...)>;

template <typename Sender>
UNIFEX_CONCEPT_FRAGMENT(     //
  _single_typed_sender_impl, //
    requires()(0) &&         //
    sender_traits<remove_cvref_t<Sender>>
      ::template value_types<
        _is_single_valued_variant,
        _is_single_valued_tuple>::value);

template <typename Sender>
UNIFEX_CONCEPT _single_typed_sender =
  typed_sender<Sender> && UNIFEX_FRAGMENT(_single_typed_sender_impl, Sender);

enum class state {empty, value, exception, done};

template <typename T>
struct _res {
  struct type {
    state state_ = state::empty;
    union {
      manual_lifetime<T> value_;
      manual_lifetime<std::exception_ptr> exception_;
    };
    coro::coroutine_handle<> continuation_ {};
    type() {}
    ~type() {
      switch(state_) {
      case state::value:
        unifex::deactivate(value_);
        break;
      case state::exception:
        unifex::deactivate(exception_);
        break;
      default:;
      }
    }
  };
};
template <typename T>
using _result = typename _res<T>::type;

template <typename T>
struct _rec {
  static_assert(!std::is_void_v<T>);
  struct type {
    _result<T> *res_;

    template(class... Us)
      (requires constructible_from<T, Us...>)
    void set_value(Us&&... us) &&
        noexcept(std::is_nothrow_constructible_v<T, Us...>) {
      unifex::activate(res_->value_, (Us&&) us...);
      res_->state_ = state::value;
      res_->continuation_.resume();
    }
    void set_error(std::exception_ptr eptr) && noexcept {
      unifex::activate(res_->exception_, std::move(eptr));
      res_->state_ = state::exception;
      res_->continuation_.resume();
    }
    void set_done() && noexcept {
      res_->state_ = state::done;
      res_->continuation_.resume();
    }
  };
};
template <typename Sender>
using _receiver_for = typename _rec<single_value_result_t<Sender>>::type;

template <typename Sender>
struct _as_await {
  struct type {
    using value_t = single_value_result_t<Sender>;
    _result<value_t> res_;
    connect_result_t<Sender, _receiver_for<Sender>> op_;

    explicit type(Sender&& sender)
      : res_{}
      , op_{connect((Sender&&) sender, _receiver_for<Sender>{&res_})} {}

    static constexpr bool await_ready() noexcept {
      return false;
    }

    void await_suspend(coro::coroutine_handle<> continuation) noexcept {
      res_.continuation_ = continuation;
      start(op_);
    }

    std::optional<value_t> await_resume() {
      switch (res_.state_) {
      case state::value:
        return std::move(res_.value_).get();
      case state::done:
        return std::nullopt;
      default:
        assert(res_.state_ == state::exception);
        std::rethrow_exception(res_.exception_.get());
      }
    }
  };
};
template <typename Sender>
using _as_awaitable = typename _as_await<remove_cvref_t<Sender>>::type;

struct with_awaitable_senders {
  template <typename Sender>
  decltype(auto) await_transform(Sender&& sender) {
    if constexpr (detail::_awaitable<Sender>) {
      return (Sender&&) sender;
    } else if constexpr (_single_typed_sender<Sender>) {
      if constexpr (sender_to<Sender, _receiver_for<Sender>>) {
        // TODO inherit context from coroutine.
        return _as_awaitable<Sender>{(Sender&&) sender};
      } else {
        return (Sender&&) sender;
      }
    } else {
      return (Sender&&) sender;
    }
  }
};

template <typename T>
struct _task {
  struct type;
};
template <typename T>
struct _task<T>::type {
  struct promise_type : with_awaitable_senders {
    void reset_value() noexcept {
      switch (std::exchange(state_, state::empty)) {
        case state::value:
          unifex::deactivate(value_);
          break;
        case state::exception:
          unifex::deactivate(exception_);
          break;
        default:
          break;
      }
    }

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
      unifex::activate(exception_, std::current_exception());
      state_ = state::exception;
    }

    template(typename Value)
        (requires convertible_to<Value, T>)
    void return_value(Value&& value) noexcept(
        std::is_nothrow_constructible_v<T, Value>) {
      reset_value();
      unifex::activate(value_, (Value &&) value);
      state_ = state::value;
    }

    promise_type() noexcept {}

    ~promise_type() {
      reset_value();
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
