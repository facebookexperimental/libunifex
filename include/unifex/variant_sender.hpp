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
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/blocking.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/manual_lifetime.hpp>

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _variant_sender {

template <typename... Ops>
struct _op {
  struct type;
};
template <typename... Ops>
using operation = typename _op<Ops...>::type;

template <typename... Ops>
struct _op<Ops...>::type {
  template <typename Sender, typename Receiver>
  type(Sender&& sender, Receiver&& receiver) noexcept(
      is_nothrow_connectable_v<Sender, Receiver>)
    : variantOp_(std::in_place_type_t<
                 manual_lifetime<connect_result_t<Sender, Receiver>>>{}) {
    using op_t = connect_result_t<Sender, Receiver>;
    std::get<manual_lifetime<op_t>>(variantOp_)
        .construct_with([&]() noexcept(
                            is_nothrow_connectable_v<Sender, Receiver>) {
          return unifex::connect(
              static_cast<Sender&&>(sender), static_cast<Receiver&&>(receiver));
        });
  }

  type(type&&) = delete;

  ~type() {
    std::visit([](auto& op){
      op.destruct();
    }, variantOp_);
  }

  void start() & noexcept {
    std::visit([](auto& op) noexcept { unifex::start(op.get()); }, variantOp_);
  }

  std::variant<manual_lifetime<Ops>...> variantOp_;
};

template <typename Sender, typename... Rest>
struct max_blocking_kind {
  constexpr auto operator()() noexcept { return cblocking<Sender>(); }
};

template <typename First, typename Second, typename... Rest>
struct max_blocking_kind<First, Second, Rest...> {
  constexpr auto operator()() noexcept {
    constexpr blocking_kind first = cblocking<First>();
    constexpr blocking_kind second = cblocking<Second>();

    if constexpr (first == second) {
      return max_blocking_kind<First, Rest...>{}();
    } else if constexpr (
        first == blocking_kind::always &&
        second == blocking_kind::always_inline) {
      return max_blocking_kind<First, Rest...>{}();
    } else if constexpr (
        first == blocking_kind::always_inline &&
        second == blocking_kind::always) {
      return max_blocking_kind<Second, Rest...>{}();
    } else {
      return blocking_kind::maybe;
    }
  }
};

template <typename... Senders>
struct _sender {
  class type;
};
template <typename... Senders>
using sender = typename _sender<remove_cvref_t<Senders>...>::type;

template <typename... Senders>
class _sender<Senders...>::type {
  std::variant<Senders...> senderVariant_;

 public:

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = typename concat_type_lists_unique_t<sender_value_types_t<Senders, type_list, Tuple>...>::template apply<Variant>;

  template <template <typename...> class Variant>
  using error_types = concat_type_lists_unique_t<sender_error_types_t<Senders, Variant>...>;

  static constexpr bool sends_done = std::disjunction_v<std::bool_constant<sender_traits<Senders>::sends_done>...>;

  template<typename ConcreteSender>
  type(ConcreteSender&& concreteSender)
    noexcept(std::is_nothrow_constructible_v<std::variant<Senders...>, decltype(concreteSender)>)
    : senderVariant_(std::forward<ConcreteSender>(concreteSender)) {}

  template<typename Base, typename Matchee>
  using match_reference_t = std::conditional_t<std::is_lvalue_reference_v<Base>, std::add_lvalue_reference_t<Matchee>, Matchee>;

  template(typename This, typename Receiver)
    (requires same_as<remove_cvref_t<This>, type> AND std::conjunction_v<std::bool_constant<sender_to<member_t<This, Senders>, Receiver>>...>)
  friend auto tag_invoke(tag_t<connect>, This&& that, Receiver&& r)
    noexcept(std::conjunction_v<unifex::is_nothrow_connectable<match_reference_t<This, Senders>, Receiver>...>)
  {
    // MSVC needs this type alias declared outside the lambda below to reliably compile
    // the visit() expression as C++20
    using op_t = operation<connect_result_t<Senders, Receiver>...>;
    return std::visit(
        [&r](auto&& sender) noexcept(
            unifex::is_nothrow_connectable_v<decltype(sender), Receiver>) {
          // MSVC doesn't like static_cast<Receiver&&>(r) in some cases when compiling
          // as C++20, but seems to reliably do the right thing with
          // static_cast<decltype(r)>(r)
          return op_t{
             static_cast<decltype(sender)&&>(sender), static_cast<decltype(r)>(r)};
        },
        static_cast<decltype(that)>(that).senderVariant_);
  }

  friend constexpr auto tag_invoke(tag_t<blocking>, const type&) noexcept {
    return _variant_sender::max_blocking_kind<Senders...>{}();
  }
};
} // namespace _variant_sender

template <typename... Senders>
using variant_sender = typename _variant_sender::sender<Senders...>;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
