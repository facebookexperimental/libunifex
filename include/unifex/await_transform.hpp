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

#if UNIFEX_NO_COROUTINES
# error "Coroutine support is required to use <unifex/await_transform.hpp>"
#endif

#include <unifex/async_trace.hpp>
#include <unifex/coroutine_concepts.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/manual_lifetime.hpp>

#include <exception>
#include <optional>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _util {
enum class _state { empty, value, exception, done };

template <typename Value>
struct _expected {
  _expected() noexcept {}
  void reset_value() noexcept {
    _reset_value(std::exchange(state_, _state::empty));
  }
  ~_expected() {
    _reset_value(state_);
  }
  _state state_ = _state::empty;
  union {
    manual_lifetime<Value> value_;
    manual_lifetime<std::exception_ptr> exception_;
  };
private:
  void _reset_value(_state s) noexcept {
    switch(s) {
    case _state::value:
      unifex::deactivate_union_member(value_);
      break;
    case _state::exception:
      unifex::deactivate_union_member(exception_);
      break;
    default:;
    }
  }
};
} // namespace _util

namespace _await_tfx {
using namespace _util;

template <typename Promise, typename Value>
struct _awaitable_base {
  struct type;
};

template <typename Promise, typename Sender>
struct _awaitable {
  struct type;
};

template <typename Promise, typename Value>
struct _awaitable_base<Promise, Value>::type {
  struct _rec {
  public:
    explicit _rec(_expected<Value>* result, coro::coroutine_handle<Promise> continuation) noexcept
      : result_(result)
      , continuation_(continuation)
    {}

    _rec(_rec&& r) noexcept
      : result_(std::exchange(r.result_, nullptr))
      , continuation_(std::exchange(r.continuation_, nullptr))
    {}

    template(class... Us)
      (requires (constructible_from<Value, Us...> ||
          (std::is_void_v<Value> && sizeof...(Us) == 0)))
    void set_value(Us&&... us) &&
        noexcept(std::is_nothrow_constructible_v<Value, Us...> ||
            std::is_void_v<Value>) {
      unifex::activate_union_member(result_->value_, (Us&&) us...);
      result_->state_ = _state::value;
      continuation_.resume();
    }

    void set_error(std::exception_ptr eptr) && noexcept {
      unifex::activate_union_member(result_->exception_, std::move(eptr));
      result_->state_ = _state::exception;
      continuation_.resume();
    }
    
    void set_done() && noexcept {
      result_->state_ = _state::done;
      continuation_.promise().unhandled_done().resume();
    }

    template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO> AND is_callable_v<CPO, const Promise&>)
    friend auto tag_invoke(CPO cpo, const _rec& r)
        noexcept(is_nothrow_callable_v<CPO, const Promise&>)
        -> callable_result_t<CPO, const Promise&> {
      const Promise& p = r.continuation_.promise();
      return std::move(cpo)(p);
    }

  #if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
    template <typename Func>
    friend void
    tag_invoke(tag_t<visit_continuations>, const _rec& r, Func&& func) {
      visit_continuations(r.continuation_.promise(), (Func&&)func);
    }
  #endif

  private:
    _expected<Value>* result_;
    coro::coroutine_handle<Promise> continuation_;
  };

  bool await_ready() const noexcept {
    return false;
  }

  Value await_resume() {
    switch (result_.state_) {
    case _state::value:
      return std::move(result_.value_).get();
    default:
      UNIFEX_ASSERT(result_.state_ == _state::exception);
      std::rethrow_exception(result_.exception_.get());
    }
  }

protected:
  _expected<Value> result_;
};

template <typename Promise, typename Sender>
using _awaitable_base_t =
  typename _awaitable_base<
    Promise,
    sender_single_value_return_type_t<remove_cvref_t<Sender>>>::type;

template <typename Promise, typename Sender>
using _receiver_t = typename _awaitable_base_t<Promise, Sender>::_rec;

template <typename Promise, typename Sender>
struct _awaitable<Promise, Sender>::type
  : _awaitable_base_t<Promise, Sender> {
private:
  using _rec = _receiver_t<Promise, Sender>;
  connect_result_t<Sender, _rec> op_;
public:
  explicit type(Sender&& sender, coro::coroutine_handle<Promise> h)
    noexcept(is_nothrow_connectable_v<Sender, _rec>)
  : op_(unifex::connect((Sender&&)sender, _rec{&this->result_, h}))
  {}

  void await_suspend(coro::coroutine_handle<Promise>) noexcept {
    unifex::start(op_);
  }
};

template <typename Promise, typename Sender>
using _as_awaitable = typename _awaitable<Promise, Sender>::type;

struct _fn {
  // Call custom implementation if present.
  template(typename Promise, typename Value)
    (requires tag_invocable<_fn, Promise&, Value>)
  auto operator()(Promise& promise, Value&& value) const
    noexcept(is_nothrow_tag_invocable_v<_fn, Promise&, Value>)
    -> tag_invoke_result_t<_fn, Promise&, Value> {
    static_assert(detail::_awaitable<tag_invoke_result_t<_fn, Promise&, Value>>,
        "The return type of a customization of unifex::await_transform() "
        "must satisfy the awaitable concept.");
    return unifex::tag_invoke(_fn{}, promise, (Value&&)value);
  }

  // Default implementation.
  template(typename Promise, typename Value)
    (requires (!tag_invocable<_fn, Promise&, Value>))
  decltype(auto) operator()(Promise& promise, Value&& value) const {
    // Note we don't fold the two '(Value&&) value'-returning cases here
    // to avoid instantiating 'unifex::sender<Value>' concept check in
    // the case that _awaitable<Value> evaluates to true.
    if constexpr (detail::_awaitable<Value>) {
      return (Value&&) value;
    } else if constexpr (unifex::sender<Value>) {
      if constexpr (unifex::sender_to<Value, _receiver_t<Promise, Value>>) {
        auto h = coro::coroutine_handle<Promise>::from_promise(promise);
        return _as_awaitable<Promise, Value>{(Value&&) value, h};
      } else {
        static_assert(
          unifex::sender_to<Value, _receiver_t<Promise, Value>>,
          "This sender is not awaitable in this coroutine type.");
        return (Value&&) value;
      }
    } else {
      return (Value&&) value;
    }
  }
};

} // namespace _await_tfx

// The await_transform() customisation point allows value-types to customise
// what kind of awaitable object should be used for this type when it is used
// within a co_await expression. It is similar to 'operator co_await()' but
// works around limitations of 'operator co_await()' by providing access to
// the promise object and promise type so that different awaitable types can
// be returned depending on the awaiting context.
//
// Coroutine promise_types can implement their .await_transform() methods to
// forward to this customisation point to enable use of type customisations.
inline constexpr _await_tfx::_fn await_transform {};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
