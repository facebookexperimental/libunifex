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
  void start() & noexcept {
    std::visit([](auto& op) noexcept { unifex::start(op); }, variantOp_);
  }

  std::variant<Ops...> variantOp_;
};


template <_block::_enum First, _block::_enum... Rest>
struct max_blocking_kind {
  constexpr auto operator()() noexcept { return First; }
};

template <_block::_enum First, _block::_enum Second, _block::_enum... Rest>
struct max_blocking_kind<First, Second, Rest...> {
  constexpr auto operator()() noexcept {
    if constexpr (First == Second) {
      return max_blocking_kind<First, Rest...>{}();
    } else if constexpr (
        (First == blocking_kind::always &&
        Second == blocking_kind::always_inline) ||
        (Second == blocking_kind::always &&
        First == blocking_kind::always_inline)) {
      return max_blocking_kind<blocking_kind::always, Rest...>{}();
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
    return operation<connect_result_t<Senders, Receiver>...>{
        std::visit([&r](auto&& sender) noexcept(unifex::is_nothrow_connectable_v<decltype(sender), Receiver>) {
            return std::variant<connect_result_t<Senders, Receiver>...>{unifex::connect(std::move(sender), static_cast<Receiver&&>(r))};
        }, std::move(static_cast<decltype(that)>(that).senderVariant_))
    };
  }

  friend constexpr auto tag_invoke(tag_t<blocking>, const type&) noexcept {
    return _variant_sender::max_blocking_kind<cblocking<Senders>...>{}();
  }
};
} // namespace _variant_sender

template <typename... Senders>
using variant_sender = typename _variant_sender::sender<Senders...>;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
