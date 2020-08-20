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
#include <unifex/await_transform.hpp>
#include <unifex/coroutine_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/type_traits.hpp>

#if UNIFEX_NO_COROUTINES
# error "Coroutine support is required to use <unifex/connect_awaitable.hpp>"
#endif

#include <cassert>
#include <optional>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _await {

template<typename Receiver>
struct _sender_task {
  class type;
};
template<typename Receiver>
using sender_task = typename _sender_task<Receiver>::type;

template<typename Receiver>
class _sender_task<Receiver>::type {
public:
  struct promise_type {
    template <typename Awaitable>
    explicit promise_type(Awaitable&, Receiver& r) noexcept
        : receiver_(r)
    {}

    type get_return_object() noexcept {
      return type{
          coro::coroutine_handle<promise_type>::from_promise(
              *this)};
    }
    coro::suspend_always initial_suspend() noexcept {
      return {};
    }
    [[noreturn]] coro::suspend_always final_suspend() noexcept {
      std::terminate();
    }
    [[noreturn]] void unhandled_exception() noexcept {
      std::terminate();
    }
    [[noreturn]] void return_void() noexcept {
      std::terminate();
    }

    template <typename Func>
    auto yield_value(Func&& func) noexcept {
      struct awaiter {
        Func&& func_;
        bool await_ready() noexcept {
          return false;
        }
        void await_suspend(coro::coroutine_handle<promise_type>) {
          ((Func &&) func_)();
        }
        [[noreturn]] void await_resume() noexcept {
          std::terminate();
        }
      };
      return awaiter{(Func &&) func};
    }

    template(typename Value)
      (requires callable<decltype(unifex::await_transform), promise_type&, Value>)
    auto await_transform(Value&& value)
        noexcept(is_nothrow_callable_v<decltype(unifex::await_transform), promise_type&, Value>)
        -> callable_result_t<decltype(unifex::await_transform), promise_type&, Value> {
      return unifex::await_transform(*this, (Value&&)value);
    }

    template <typename Func>
    friend void
    tag_invoke(tag_t<visit_continuations>, const promise_type& p, Func&& func) {
      visit_continuations(p.receiver_, (Func&&)func);
    }

    template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO> AND
                is_callable_v<CPO, const Receiver&>)
    friend auto tag_invoke(CPO cpo, const promise_type& p)
        noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
        -> callable_result_t<CPO, const Receiver&> {
      return cpo(std::as_const(p.receiver_));
    }

    Receiver& receiver_;
  };

  coro::coroutine_handle<promise_type> coro_;

  explicit type(
      coro::coroutine_handle<promise_type> coro) noexcept
      : coro_(coro) {}

  type(type&& other) noexcept
      : coro_(std::exchange(other.coro_, {})) {}

  ~type() {
    if (coro_)
      coro_.destroy();
  }

  void start() & noexcept {
    coro_.resume();
  }
};

} // namespace _await

namespace _await_cpo {
  template<typename... Ts>
  using count_types = std::integral_constant<std::size_t, sizeof...(Ts)>;

  inline const struct _fn {
  private:
    template <typename Awaitable, typename Receiver>
    static auto connect_impl(Awaitable awaitable, Receiver receiver)
        -> _await::sender_task<Receiver> {
#if !UNIFEX_NO_EXCEPTIONS
      std::exception_ptr ex;
      try {
#endif // !UNIFEX_NO_EXCEPTIONS

        using result_type = std::optional<single_value_result_t<Awaitable>>;

        // This is a bit mind bending control-flow wise.
        // We are first evaluating the co_await expression.
        // Then the result of that is passed into std::invoke
        // which curries a reference to the result into another
        // lambda which is then returned to 'co_yield'.
        // The 'co_yield' expression then invokes this lambda
        // after the coroutine is suspended so that it is safe
        // for the receiver to destroy the coroutine.
        co_yield [&](result_type&& result) {
              return [&] {
                constexpr bool canCompleteWithValue = Awaitable::template value_types<count_types, single_value_type>::value > 0;
                if constexpr (canCompleteWithValue) {
                  using value_type = typename Awaitable::template value_types<single_type, single_value_type>;
                  if (result.has_value()) {
                    if constexpr (std::is_void_v<value_type>) {
                      unifex::set_value(std::move(receiver));
                    } else {
                      unifex::set_value(std::move(receiver), std::move(result).value());
                    }
                    return;
                  }
                }

                assert(!result.has_value());

                unifex::set_done(std::move(receiver));
              };
            }(co_await (Awaitable &&) awaitable);
#if !UNIFEX_NO_EXCEPTIONS
      } catch (...) {
        ex = std::current_exception();
      }
      co_yield[&] {
        unifex::set_error(std::move(receiver), std::move(ex));
      };
#endif // !UNIFEX_NO_EXCEPTIONS
    }

  public:
    template <typename Awaitable, typename Receiver>
    auto operator()(Awaitable&& awaitable, Receiver&& receiver) const
      -> _await::sender_task<remove_cvref_t<Receiver>> {
      return connect_impl((Awaitable&&)awaitable, (Receiver&&)receiver);
    }
  } connect_awaitable{};
} // namespace _await_cpo

using _await_cpo::connect_awaitable;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
