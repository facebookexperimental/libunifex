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

    coro::coroutine_handle<> unhandled_done() noexcept {
      unifex::set_done(std::move(receiver_));
      return coro::noop_coroutine();
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

    template <typename Value>
    auto await_transform(Value&& value) -> decltype(auto) {
      if constexpr (callable<decltype(unifex::await_transform), promise_type&, Value>) {
        return unifex::await_transform(*this, (Value&&)value);
      } else {
        return Value((Value &&) value);
      }
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

  template<typename Receiver>
  struct set_value_applicator {
    Receiver& receiver_;

    template<typename... Values>
    void operator()(Values&&... values) {
      unifex::set_value(std::move(receiver_), (Values&&)values...);
    }
  };

  inline const struct _fn {
  private:
    struct _comma_hack {
      template <typename T>
      friend T&& operator,(T&& t, _comma_hack) noexcept {
        return (T&&) t;
      }
      operator unit() const noexcept { return {}; }
    };
    template <typename Awaitable, typename Receiver>
    static auto connect_impl(Awaitable awaitable, Receiver receiver)
        -> _await::sender_task<Receiver> {
#if !UNIFEX_NO_EXCEPTIONS
      std::exception_ptr ex;
      try {
#endif // !UNIFEX_NO_EXCEPTIONS

        using result_type = sender_single_value_result_t<Awaitable>;

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
                constexpr size_t valueOverloadCount =
                    sender_value_types_t<Awaitable, count_types, single_value_type>::value;
                static_assert(valueOverloadCount <= 1);

                if constexpr (valueOverloadCount == 1) {
                  constexpr size_t valueCount =
                      sender_value_types_t<Awaitable, type_identity_t, count_types>::value;
                  if constexpr (valueCount == 0) {
                    unifex::set_value(std::move(receiver));
                  } else if constexpr (valueCount == 1) {
                    unifex::set_value(std::move(receiver), static_cast<result_type&&>(result));
                  } else {
                    std::apply(set_value_applicator<Receiver>{receiver}, (result_type&&)result);
                  }
                } else {
                  // Shouldn't complete with a value if there are no value_types
                  // specified.
                  std::terminate();
                }
              };
            // The _comma_hack here makes this well-formed when the co_await
            // expression has type void. This could potentially run into trouble
            // if the type of the co_await expression itself overloads operator
            // comma, but that's pretty unlikely.
            }((co_await (Awaitable &&)awaitable, _comma_hack{}));
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
