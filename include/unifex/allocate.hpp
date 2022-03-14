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

#include <unifex/config.hpp>
#include <unifex/get_allocator.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/bind_back.hpp>
#include <unifex/blocking.hpp>

#include <memory>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

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
      scope_guard freeOnError = [&]() noexcept {
        allocator_traits::deallocate(allocator_, op, 1);
      };
      op_ = ::new (static_cast<void*>(op))
          Operation(connect((Sender &&) s, (Receiver &&) r));
      freeOnError.release();
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
    using value_types = sender_value_types_t<Sender, Variant, Tuple>;

    template <template <typename...> class Variant>
    using error_types = sender_error_types_t<Sender, Variant>;

    static constexpr bool sends_done = sender_traits<Sender>::sends_done;

    template(typename Self, typename Receiver)
      (requires same_as<remove_cvref_t<Self>, type> AND
                receiver<Receiver>)
    friend auto tag_invoke(tag_t<connect>, Self&& s, Receiver&& r)
        -> operation<
            connect_result_t<member_t<Self, Sender>, Receiver>,
            remove_cvref_t<get_allocator_t<Receiver>>> {
      return operation<
          connect_result_t<member_t<Self, Sender>, Receiver>,
          remove_cvref_t<get_allocator_t<Receiver>>>{
          static_cast<Self&&>(s).sender_, (Receiver &&) r};
    }

    friend constexpr auto tag_invoke(tag_t<unifex::blocking>, const type& self) noexcept {
      return blocking(self.sender_);
    }

    Sender sender_;
  };
} // namespace _alloc

namespace _alloc_cpo {
  inline const struct _fn {
  private:
    template <typename Sender>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, Sender>,
        meta_tag_invoke_result<_fn>,
        meta_quote1<_alloc::sender>>::template apply<Sender>;
  public:
    template(typename Sender)
      (requires tag_invocable<_fn, Sender>)
    auto operator()(Sender&& predecessor) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Sender>)
        -> _result_t<Sender> {
      return unifex::tag_invoke(_fn{}, (Sender&&) predecessor);
    }
    template(typename Sender)
      (requires (!tag_invocable<_fn, Sender>))
    auto operator()(Sender&& predecessor) const
        noexcept(std::is_nothrow_constructible_v<_alloc::sender<Sender>, Sender>)
        -> _result_t<Sender> {
      return _alloc::sender<Sender>{(Sender &&) predecessor};
    }
    constexpr auto operator()() const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn>)
        -> bind_back_result_t<_fn> {
      return bind_back(*this);
    }
  } allocate{};
} // namespace _alloc_cpo
using _alloc_cpo::allocate;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
