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

#include <unifex/blocking.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/config.hpp>
#include <unifex/get_allocator.hpp>
#include <unifex/scope_guard.hpp>

namespace unifex {
namespace _submit {
template <typename Sender, typename Receiver>
struct _op {
  class type;
};
template <typename Sender, typename Receiver>
using operation = typename _op<Sender, std::remove_cvref_t<Receiver>>::type;

template <typename Sender, typename Receiver>
class _op<Sender, Receiver>::type {
  class wrapped_receiver {
    type* op_;

  public:
    explicit wrapped_receiver(type* op) noexcept : op_(op) {}

    template <typename... Values>
    void set_value(Values&&... values) && noexcept {
      auto allocator = get_allocator(op_->receiver_);
      unifex::set_value(std::move(op_->receiver_), (Values &&) values...);
      destroy(std::move(allocator));
    }

    template <typename Error>
    void set_error(Error&& error) && noexcept {
      auto allocator = get_allocator(op_->receiver_);
      unifex::set_error(std::move(op_->receiver_), (Error &&) error);
      destroy(std::move(allocator));
    }

    void set_done() && noexcept {
      auto allocator = get_allocator(op_->receiver_);
      unifex::set_done(std::move(op_->receiver_));
      destroy(std::move(allocator));
    }

  private:

    template<typename Allocator>
    void destroy(Allocator allocator) noexcept {
      using allocator_traits = std::allocator_traits<Allocator>;
      using typed_allocator = typename allocator_traits::template rebind_alloc<type>;
      using typed_allocator_traits = std::allocator_traits<typed_allocator>;
      typed_allocator typedAllocator{allocator};
      typed_allocator_traits::destroy(typedAllocator, op_);
      typed_allocator_traits::deallocate(typedAllocator, op_, 1);
    }

    Receiver& get_receiver() const { return op_->receiver_; }

    template <
        typename CPO,
        std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
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
      std::invoke(func, r.op_->receiver_);
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
  /*UNIFEX_NO_UNIQUE_ADDRESS*/ operation_t<Sender, wrapped_receiver> inner_;
};
} // namespace _submit

namespace _submit_cpo {
  inline constexpr struct submit_cpo {
    template<typename Sender, typename Receiver>
    void operator()(Sender&& sender, Receiver&& receiver) const {
      if constexpr (is_tag_invocable_v<submit_cpo, Sender, Receiver>) {
        static_assert(
          std::is_same_v<tag_invoke_result_t<submit_cpo, Sender, Receiver>>,
          "Customisations of submit() must have a void return value");
        unifex::tag_invoke(*this, (Sender&&)sender, (Receiver&&)receiver);
      } else {
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
            using operation_type = _submit::operation<Sender, Receiver>;

            operation_type* op = nullptr;
            {
              // Use the receiver's associated allocator to allocate this state.
              auto allocator = get_allocator(receiver);
              using allocator_traits = std::allocator_traits<decltype(allocator)>;
              using typed_allocator = typename allocator_traits::template rebind_alloc<operation_type>;
              using typed_allocator_traits = std::allocator_traits<typed_allocator>;

              typed_allocator typedAllocator{allocator};
              op = typed_allocator_traits::allocate(typedAllocator, 1);
              bool constructorSucceeded = false;
              scope_guard freeOnError = [&]() noexcept {
                if (!constructorSucceeded) {
                  typed_allocator_traits::deallocate(typedAllocator, op, 1);
                }
              };
              typed_allocator_traits::construct(typedAllocator, op, (Sender&&)sender, (Receiver&&)receiver);
              constructorSucceeded = true;
            }
            op->start();
          }
        }
      }
    }
  } submit{};
} // namespace _submit_cpo
using _submit_cpo::submit;

} // namespace unifex
