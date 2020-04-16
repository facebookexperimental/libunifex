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

#include <unifex/config.hpp>
#include <unifex/get_allocator.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sender_concepts.hpp>

#include <memory>
#include <type_traits>

namespace unifex {
namespace _alloc {
  template <typename Operation, typename Allocator>
  struct _op {
    class type;
  };
  template <typename Operation, typename Allocator>
  using operation = typename _op<Operation, Allocator>::type;

  template <typename Operation, typename Allocator>
  class _op<Operation, Allocator>::type {
    using operation = type;
    using allocator_t = typename std::allocator_traits<
        Allocator>::template rebind_alloc<Operation>;

  public:
    template <typename Sender, typename Receiver>
    explicit type(Sender&& s, Receiver&& r)
      : allocator_(get_allocator(r)) {
      using allocator_traits = std::allocator_traits<allocator_t>;
      Operation* op = allocator_traits::allocate(allocator_, 1);
      bool constructorSucceeded = false;
      scope_guard freeOnError = [&]() noexcept {
        if (!constructorSucceeded) {
          allocator_traits::deallocate(allocator_, op, 1);
        }
      };
      op_ = ::new (static_cast<void*>(op))
          Operation(connect((Sender &&) s, (Receiver &&) r));
      constructorSucceeded = true;
    }

    ~type() {
      op_->~Operation();
      std::allocator_traits<allocator_t>::deallocate(allocator_, op_, 1);
    }

    friend void tag_invoke(tag_t<start>, operation& op) noexcept {
      start(*op.op_);
    }

  private:
    Operation* op_;
    UNIFEX_NO_UNIQUE_ADDRESS allocator_t allocator_;
  };

  template <typename Sender>
  struct _sender {
    class type;
  };
  template <typename Sender>
  using sender = typename _sender<remove_cvref_t<Sender>>::type;

  template <typename Sender>
  class _sender<Sender>::type {
    using sender = type;
  public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = typename Sender::template value_types<Variant, Tuple>;

    template <template <typename...> class Variant>
    using error_types = typename Sender::template error_types<Variant>;

    template <typename Receiver>
    friend auto tag_invoke(tag_t<connect>, sender&& s, Receiver&& r)
        -> operation<
            operation_t<Sender, Receiver>,
            remove_cvref_t<get_allocator_t<Receiver>>> {
      return operation<
          operation_t<Sender, Receiver>,
          remove_cvref_t<get_allocator_t<Receiver>>>{
          (Sender &&) s.sender_, (Receiver &&) r};
    }

    template <typename Receiver>
    friend auto tag_invoke(tag_t<connect>, sender& s, Receiver&& r)
        -> operation<
            operation_t<Sender&, Receiver>,
            remove_cvref_t<get_allocator_t<Receiver>>> {
      return operation<
          operation_t<Sender&, Receiver>,
          remove_cvref_t<get_allocator_t<Receiver>>>{
          s.sender_, (Receiver &&) r};
    }

    template <typename Receiver>
    friend auto
    tag_invoke(tag_t<connect>, const sender& s, Receiver&& r)
        -> operation<
            operation_t<const Sender&, Receiver>,
            remove_cvref_t<get_allocator_t<Receiver>>> {
      return operation<
          operation_t<const Sender&, Receiver>,
          remove_cvref_t<get_allocator_t<Receiver>>>{
          std::as_const(s.sender_), (Receiver &&) r};
    }

    Sender sender_;
  };
} // namespace _alloc

namespace _alloc_cpo {
  inline constexpr struct _fn {
  private:
    template<bool>
    struct _impl {
      template <typename Sender>
      auto operator()(Sender&& predecessor) const
          noexcept(is_nothrow_tag_invocable_v<_fn, Sender>) {
        return unifex::tag_invoke(_fn{}, (Sender&&) predecessor);
      }
    };
  public:
    template <typename Sender>
    auto operator()(Sender&& predecessor) const
        noexcept(is_nothrow_callable_v<
          _impl<is_tag_invocable_v<_fn, Sender>>, Sender>) {
      return _impl<is_tag_invocable_v<_fn, Sender>>{}((Sender&&) predecessor);
    }
  } allocate{};

  template<>
  struct _fn::_impl<false> {
    template <typename Sender>
    auto operator()(Sender&& predecessor) const
        noexcept(std::is_nothrow_constructible_v<_alloc::sender<Sender>, Sender>)
        -> _alloc::sender<Sender> {
      return _alloc::sender<Sender>{(Sender &&) predecessor};
    }
  };
} // namespace _alloc_cpo
using _alloc_cpo::allocate;

} // namespace unifex
