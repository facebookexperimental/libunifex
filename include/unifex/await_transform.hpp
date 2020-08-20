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

#include <cassert>
#include <exception>
#include <optional>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _await_transform {

template <typename Promise, typename Sender>
class _as_awaitable {
  using value_t = typename sender_traits<remove_cvref_t<Sender>>::
        template value_types<single_overload, single_value_type>::type::type;
  
  enum class state { empty, value, exception, done };

  class receiver {
  public:
    explicit receiver(_as_awaitable* op, coro::coroutine_handle<Promise> continuation) noexcept
    : op_(op)
    , continuation_(continuation)
    {}

    receiver(receiver&& r) noexcept
    : op_(std::exchange(r.op_, nullptr))
    , continuation_(std::exchange(r.continuation_, nullptr))
    {}

    template(class... Us)
      (requires (constructible_from<value_t, Us...> || (std::is_void_v<value_t> && sizeof...(Us) == 0)))
    void set_value(Us&&... us) &&
        noexcept(std::is_nothrow_constructible_v<value_t, Us...>) {
      unifex::activate_union_member(op_->value_, (Us&&) us...);
      op_->state_ = state::value;
      continuation_.resume();
    }

    void set_error(std::exception_ptr eptr) && noexcept {
      unifex::activate_union_member(op_->exception_, std::move(eptr));
      op_->state_ = state::exception;
      continuation_.resume();
    }
    
    void set_done() && noexcept {
      // TODO: Enable this once sender_traits support 'sends_done'
      //static_assert(sender_traits<remove_cvref_t<Sender>>::sends_done);
      op_->state_ = state::done;
      continuation_.promise().unhandled_done().resume();
    }

    template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO> AND is_callable_v<CPO, const Promise&>)
    friend auto tag_invoke(CPO cpo, const receiver& r)
        noexcept(is_nothrow_callable_v<CPO, const Promise&>)
        -> callable_result_t<CPO, const Promise&> {
      const Promise& p = r.continuation_.promise();
      return std::move(cpo)(p);
    }

    template <typename Func>
    friend void
    tag_invoke(tag_t<visit_continuations>, const receiver& r, Func&& func) {
      visit_continuations(r.continuation_.promise(), (Func&&)func);
    }

  private:
    _as_awaitable* op_;
    coro::coroutine_handle<Promise> continuation_;
  };

public:
  explicit _as_awaitable(Sender&& sender, coro::coroutine_handle<Promise> h)
    noexcept(is_nothrow_connectable_v<Sender, receiver>)
  : op_(unifex::connect((Sender&&)sender, receiver{this, h}))
  {}

  ~_as_awaitable() {
    switch(state_) {
    case state::value:
      unifex::deactivate_union_member(value_);
      break;
    case state::exception:
      unifex::deactivate_union_member(exception_);
      break;
    default:;
    }
  }

  bool await_ready() const noexcept {
    return false;
  }

  void await_suspend(coro::coroutine_handle<Promise>) noexcept {
    unifex::start(op_);
  }

  value_t await_resume() {
    switch (state_) {
    case state::value:
      return std::move(value_).get();
    default:
      assert(state_ == state::exception);
      std::rethrow_exception(exception_.get());
    }
  }

private:
  state state_ = state::empty;
  union {
    manual_lifetime<value_t> value_;
    manual_lifetime<std::exception_ptr> exception_;
  };
  connect_result_t<Sender, receiver> op_;
};

struct _fn {
  // Call custom implementation if present.
  template(typename Promise, typename Value)
    (requires tag_invocable<_fn, Promise&, Value>)
  auto operator()(Promise& promise, Value&& value) const
    noexcept(is_nothrow_tag_invocable_v<_fn, Promise&, Value>)
    -> tag_invoke_result_t<_fn, Promise&, Value> {
    return unifex::tag_invoke(_fn{}, promise, (Value&&)value);
  }

  // Default implementation.
  template(typename Promise, typename Value)
    (requires (!tag_invocable<_fn, Promise&, Value>))
  decltype(auto) operator()(Promise& promise, Value&& value) const {
    // Note we don't fold the two '(Value&&)value'-returning cases here
    // to avoid instantiating 'unifex::sender<Value>' concept check in
    // the case that _awaitable<Value> evaluates to true.
    if constexpr (detail::_awaitable<Value>) {
      return (Value&&)value;
    } else if constexpr (unifex::sender<Value>) {
      auto h = coro::coroutine_handle<Promise>::from_promise(promise);
      return _as_awaitable<Promise, Value>{(Value&&)value, h};
    } else {
      return (Value&&)value;
    }
  }
};

} // namespace _await_transform

// The await_transform() customisation point allows value-types to customise
// what kind of awaitable object should be used for this type when it is used
// within a co_await expression. It is similar to 'operator co_await()' but
// works around limitations of 'operator co_await()' by providing access to
// the promise object and promise type so that different awaitable types can
// be returned depending on the awaiting context.
//
// Coroutine promise_types can implement their .await_transform() methods to
// forward to this customisation point to enable use of type customisations.
inline constexpr _await_transform::_fn await_transform;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
