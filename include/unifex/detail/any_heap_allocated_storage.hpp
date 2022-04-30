/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <unifex/config.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/detail/with_forwarding_tag_invoke.hpp>

#include <memory>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  namespace detail
  {
    template <typename T, typename Allocator>
    class _any_heap_allocated_storage {
    private:
      struct state;

      using allocator_type = typename std::allocator_traits<
          Allocator>::template rebind_alloc<state>;
      using allocator_traits = std::allocator_traits<allocator_type>;

      // This is the state that is actually heap-allocated.
      struct state {
        template(typename... Args)                     //
            (requires constructible_from<T, Args...>)  //
            explicit state(
                std::allocator_arg_t,
                allocator_type allocator,
                std::in_place_type_t<T>,
                Args&&... args)
          : object(static_cast<Args&&>(args)...)
          , allocator(std::move(allocator)) {}

        UNIFEX_NO_UNIQUE_ADDRESS T object;
        UNIFEX_NO_UNIQUE_ADDRESS allocator_type allocator;
      };

      // This is the base-class object that holds the pointer to the
      // heap-allocated state.
      class base {
      public:
        template(typename... Args)                     //
            (requires constructible_from<T, Args...>)  //
            explicit base(
                std::allocator_arg_t,
                allocator_type allocator,
                std::in_place_type_t<T>,
                Args&&... args) {
          state_ = allocator_traits::allocate(allocator, 1);

          auto deallocateOnExit = scope_guard{[&]() noexcept {
            allocator_traits::deallocate(allocator, state_, 1);
          }};

          allocator_traits::construct(
              allocator,
              state_,
              std::allocator_arg,
              allocator,
              std::in_place_type<T>,
              static_cast<Args&&>(args)...);

          deallocateOnExit.release();
        }

        template(typename... Args)                      //
            (requires                                   //
             (!same_as<Allocator, allocator_type>) AND  //
                 constructible_from<T, Args...>)        //
            explicit base(
                std::allocator_arg_t,
                Allocator allocator,
                std::in_place_type_t<T>,
                Args&&... args)
          : base(
                std::allocator_arg,
                allocator_type(std::move(allocator)),
                std::in_place_type<T>,
                static_cast<Args&&>(args)...) {}

        // Ideally this would only be instantiated if it's actually called.
        //
        // This should have a 'requires std::copy_constructible<T>' on the
        // definition but we can't currently do that with the concepts emulation
        // layer so we'll just enable the check when we actually have concepts.
        base(const base& other)
#if UNIFEX_CXX_CONCEPTS
            requires copy_constructible<T>
#endif
          : base(
                std::allocator_arg,
                const_cast<const allocator_type&>(other.state_->allocator),
                std::in_place_type<T>,
                const_cast<const T&>(other.state_->object)) {
        }

        base(base&& other) noexcept
          : state_(std::exchange(other.state_, nullptr)) {}

        ~base() {
          if (state_ != nullptr) {
            allocator_type allocCopy = std::move(state_->allocator);
            state_->~state();
            std::allocator_traits<allocator_type>::deallocate(
                allocCopy, state_, 1);
          }
        }

      private:
        friend T& tag_invoke(tag_t<get_wrapped_object>, base& self) noexcept {
          assert(self.state_ != nullptr);
          return self.state_->object;
        }

        friend const T&
        tag_invoke(tag_t<get_wrapped_object>, const base& self) noexcept {
          assert(self.state_ != nullptr);
          return self.state_->object;
        }

        state* state_;
      };

      template <typename... CPOs>
      struct concrete final {
        class type
          : public base
          , private detail::with_forwarding_tag_invoke<type, CPOs>... {
          using base::base;
        };
      };

    public:
      template <typename... CPOs>
      using type = typename concrete<CPOs...>::type;
    };

    template <typename T, typename Allocator, typename... CPOs>
    using any_heap_allocated_storage =
        typename _any_heap_allocated_storage<T, Allocator>::template type<
            CPOs...>;

  }  // namespace detail
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
