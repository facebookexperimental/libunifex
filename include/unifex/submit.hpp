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

#include <unifex/blocking.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/config.hpp>
#include <unifex/get_allocator.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/bind_back.hpp>

#include <memory>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _submit {
template <typename Allocator, typename T>
using rebind_alloc_t =
    typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
template <typename Allocator, typename T>
using rebind_traits_t =
    std::allocator_traits<rebind_alloc_t<Allocator, T>>;

template <typename Sender, typename Receiver>
struct _op {
  class type;
};
template <typename Sender, typename Receiver>
using operation = typename _op<Sender, remove_cvref_t<Receiver>>::type;

template <typename Sender, typename Receiver>
class _op<Sender, Receiver>::type {
  struct wrapped_receiver {
    type* op_;

    template(typename... Values)
      (requires receiver_of<Receiver, Values...>)
    void set_value(Values&&... values) && noexcept {
      auto allocator = get_allocator(get_receiver());
      unifex::set_value(std::move(get_receiver()), (Values &&) values...);
      destroy(std::move(allocator));
    }

    template(typename Error)
      (requires receiver<Receiver, Error>)
    void set_error(Error&& error) && noexcept {
      auto allocator = get_allocator(get_receiver());
      unifex::set_error(std::move(get_receiver()), (Error &&) error);
      destroy(std::move(allocator));
    }

    void set_done() && noexcept {
      auto allocator = get_allocator(get_receiver());
      unifex::set_done(std::move(get_receiver()));
      destroy(std::move(allocator));
    }

    template <typename Allocator>
    void destroy(Allocator allocator) noexcept {
      type* op = op_;
      rebind_alloc_t<Allocator, type> typedAllocator{std::move(allocator)};
      rebind_traits_t<Allocator, type>::destroy(typedAllocator, op);
      rebind_traits_t<Allocator, type>::deallocate(typedAllocator, op, 1);
    }

    Receiver& get_receiver() const noexcept {
      return op_->receiver_;
    }

    template(typename CPO)
        (requires is_receiver_query_cpo_v<CPO>)
    friend auto tag_invoke(CPO cpo, const wrapped_receiver& r) noexcept(
        is_nothrow_callable_v<CPO, const Receiver&>)
        -> callable_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.get_receiver()));
    }

    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const wrapped_receiver& r,
        Func&& func) {
      std::invoke(func, std::as_const(r.get_receiver()));
    }
  };

public:
  template <typename Receiver2>
  explicit type(Sender&& sender, Receiver2&& receiver)
    : receiver_((Receiver2 &&) receiver)
    , inner_(unifex::connect((Sender &&) sender, wrapped_receiver{this}))
  {}

  void start() & noexcept {
    unifex::start(inner_);
  }

private:
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  /*UNIFEX_NO_UNIQUE_ADDRESS*/ connect_result_t<Sender, wrapped_receiver> inner_;
};
} // namespace _submit

namespace _submit_cpo {
  using _submit::rebind_alloc_t;
  using _submit::rebind_traits_t;
  template <typename Sender, typename Receiver>
  using _member_submit_result_t =
      decltype((UNIFEX_DECLVAL(Sender&&)).submit(UNIFEX_DECLVAL(Receiver&&)));

  template <typename Sender, typename Receiver>
  UNIFEX_CONCEPT_FRAGMENT( //
    _has_member_submit_,  //
      requires() (         //
        typename(_member_submit_result_t<Sender, Receiver>)
      ));
  template <typename Sender, typename Receiver>
  UNIFEX_CONCEPT //
    _has_member_submit = //
      UNIFEX_FRAGMENT(_submit_cpo::_has_member_submit_, Sender, Receiver);

  inline const struct _fn {
    template(typename Sender, typename Receiver)
        (requires sender<Sender> AND receiver<Receiver> AND
          tag_invocable<_fn, Sender, Receiver>)
    void operator()(Sender&& sender, Receiver&& receiver) const {
      static_assert(
        std::is_void_v<tag_invoke_result_t<_fn, Sender, Receiver>>,
        "Customisations of submit() must have a void return value");
      unifex::tag_invoke(*this, (Sender&&) sender, (Receiver&&) receiver);
    }
    template(typename Sender, typename Receiver)
        (requires sender<Sender> AND receiver<Receiver> AND
          (!tag_invocable<_fn, Sender, Receiver>) AND
          _has_member_submit<Sender, Receiver>)
    void operator()(Sender&& sender, Receiver&& receiver) const {
      ((Sender&&) sender).submit((Receiver&&) receiver);
    }
    template(typename Sender, typename Receiver)
        (requires sender<Sender> AND receiver<Receiver> AND
          (!tag_invocable<_fn, Sender, Receiver>) AND
          (!_has_member_submit<Sender, Receiver>) AND
          sender_to<Sender, Receiver>)
    void operator()(Sender&& sender, Receiver&& receiver) const {
      // Default implementation in terms of connect/start
      switch (blocking(sender)) {
        case blocking_kind::always:
        case blocking_kind::always_inline:
        {
          // The sender will complete synchronously so we can avoid allocating the
          // state on the heap.
          auto op = unifex::connect((Sender &&) sender, (Receiver &&) receiver);
          unifex::start(op);
          break;
        }
        default:
        {
          // Otherwise need to heap-allocate the operation-state
          using op_t = _submit::operation<Sender, Receiver>;
          op_t* op = nullptr;

          {
            // Use the receiver's associated allocator to allocate this state.
            auto allocator = get_allocator(receiver);
            using allocator_t = decltype(allocator);

            rebind_alloc_t<allocator_t, op_t> typedAllocator{allocator};
            op = rebind_traits_t<allocator_t, op_t>::allocate(typedAllocator, 1);
            scope_guard freeOnError = [&]() noexcept {
              rebind_traits_t<allocator_t, op_t>::deallocate(typedAllocator, op, 1);
            };
            rebind_traits_t<allocator_t, op_t>::construct(
                typedAllocator, op, (Sender&&)sender, (Receiver&&)receiver);
            freeOnError.release();
          }
          op->start();
        }
      }
    }
    template(typename Receiver)
        (requires receiver<Receiver>)
    constexpr auto operator()(Receiver&& receiver) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, Receiver>)
        -> bind_back_result_t<_fn, Receiver> {
      return bind_back(*this, (Receiver&&)receiver);
    }
  } submit{};
} // namespace _submit_cpo
using _submit_cpo::submit;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
