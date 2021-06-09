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
      return unifex::await_transform(*this, (Value&&) value);
    }

  #if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
    template <typename Func>
    friend void
    tag_invoke(tag_t<visit_continuations>, const promise_type& p, Func&& func) {
      visit_continuations(p.receiver_, (Func&&) func);
    }
  #endif

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
  inline const struct _fn {
  private:
    template <typename Awaitable>
    using awaitable_single_value_result_t =
        non_void_t<wrap_reference_t<decay_rvalue_t<await_result_t<Awaitable>>>>;

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

        // The _sender_task's promise type has an await_transform that passes the
        // awaitable through unifex::await_transform. So take that into consideration
        // when computing the result type:
        using promise_type = typename _await::sender_task<Receiver>::promise_type;
        using awaitable_type =
            callable_result_t<tag_t<unifex::await_transform>, promise_type&, Awaitable>;
        using result_type = awaitable_single_value_result_t<awaitable_type>;

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
                if constexpr (std::is_void_v<await_result_t<awaitable_type>>) {
                  unifex::set_value(std::move(receiver));
                } else {
                  unifex::set_value(std::move(receiver), static_cast<result_type&&>(result));
                }
              };
            // The _comma_hack here makes this well-formed when the co_await
            // expression has type void. This could potentially run into trouble
            // if the type of the co_await expression itself overloads operator
            // comma, but that's pretty unlikely.
            }((co_await (Awaitable &&) awaitable, _comma_hack{}));
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
      return connect_impl((Awaitable&&) awaitable, (Receiver&&) receiver);
    }
  } connect_awaitable{};
} // namespace _await_cpo

using _await_cpo::connect_awaitable;

// as_sender, for adapting an awaitable to be a typed sender
namespace _as_sender {
  template <typename Awaitable, typename Result = await_result_t<Awaitable>>
  struct _sndr {
    struct type {
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types = Variant<Tuple<Result>>;

      template <template <typename...> class Variant>
      using error_types = Variant<std::exception_ptr>;

      static constexpr bool sends_done = true;

      type(Awaitable awaitable)
        : awaitable_((Awaitable&&) awaitable)
      {}

      template (typename Receiver)
        (requires receiver_of<Receiver, Result>)
      friend auto tag_invoke(tag_t<unifex::connect>, type&& t, Receiver&& r) {
        return unifex::connect_awaitable(((type&&) t).awaitable_, (Receiver&&) r);
      }

      friend constexpr auto tag_invoke(tag_t<unifex::blocking>, const type& t) noexcept {
        return unifex::blocking(t.awaitable_);
      }
    private:
      Awaitable awaitable_;
    };
  };

  template <typename Awaitable>
  struct _sndr<Awaitable, void> {
    struct type {
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types = Variant<Tuple<>>;

      template <template <typename...> class Variant>
      using error_types = Variant<std::exception_ptr>;

      static constexpr bool sends_done = true;

      explicit type(Awaitable awaitable)
          noexcept(std::is_nothrow_move_constructible_v<Awaitable>)
        : awaitable_((Awaitable&&) awaitable)
      {}

      template (typename Receiver)
        (requires receiver_of<Receiver>)
      friend auto tag_invoke(tag_t<unifex::connect>, type&& t, Receiver&& r) {
        return unifex::connect_awaitable(((type&&) t).awaitable_, (Receiver&&) r);
      }

      friend constexpr auto tag_invoke(tag_t<unifex::blocking>, const type& t) noexcept {
        return unifex::blocking(t.awaitable_);
      }
    private:
      Awaitable awaitable_;
    };
  };

  template <typename Awaitable>
  using _sender = typename _sndr<Awaitable>::type;

  struct _fn {
    template (typename Awaitable)
      (requires detail::_awaitable<Awaitable>)
    _sender<remove_cvref_t<Awaitable>> operator()(Awaitable&& awaitable) const {
      return _sender<remove_cvref_t<Awaitable>>{(Awaitable&&) awaitable};
    }
  };
} // namespace _as_sender

// Transforms an awaitable into a typed sender:
inline constexpr _as_sender::_fn as_sender {};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
