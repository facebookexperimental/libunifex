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

#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>

#include <unifex/detail/lambda_op.hpp>
#include <unifex/detail/make_traits.hpp>
#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _make_traits {

struct sender_traits_literal {
  const bool sends_done = true;
  const blocking_kind blocking = blocking_kind::maybe;
  const bool is_always_scheduler_affine = false;
};

template <>
struct define_trait<&sender_traits_literal::sends_done> {
  template <bool Value>
  struct type {
    static constexpr bool sends_done = Value;
  };
};

template <>
struct define_trait<&sender_traits_literal::blocking> {
  template <blocking_kind Value>
  struct type {
    static constexpr blocking_kind blocking = Value;
  };
};

template <>
struct define_trait<&sender_traits_literal::is_always_scheduler_affine> {
  template <bool Value>
  struct type {
    static constexpr bool is_always_scheduler_affine = Value;
  };
};

template <>
struct get_traits<sender_traits_literal>
  : public define_traits<
        sender_traits_literal,
        &sender_traits_literal::sends_done,
        &sender_traits_literal::blocking,
        &sender_traits_literal::is_always_scheduler_affine> {};

}  // namespace _make_traits

template <_make_traits::sender_traits_literal Traits>
constexpr _make_traits::get_traits<_make_traits::sender_traits_literal>::type<
    Traits>
    with_sender_traits{};

namespace _create_raw_sndr {

template <typename Tr, typename Fn, typename... ValueTypes>
struct _sender : public Tr {
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<ValueTypes...>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  explicit _sender(Fn&& fn) noexcept(std::is_nothrow_move_constructible_v<Fn>)
    : fn_(std::forward<Fn>(fn)) {}

  // Overload 1: factory returns a type with start(). Returned directly.
  template <typename Receiver>
    requires receiver_of<Receiver, ValueTypes...> &&
      std::is_invocable_v<
                 _start_cpo::_fn,
                 decltype(UNIFEX_DECLVAL(Fn)(UNIFEX_DECLVAL(Receiver)))&>
  friend auto
  tag_invoke(tag_t<connect>, _sender&& self, Receiver&& rec) noexcept(
      std::is_nothrow_invocable_v<Fn, Receiver&&>) {
    return std::move(self.fn_)(std::forward<Receiver>(rec));
  }

  // Overload 2: factory returns a callable (no start()). Wrapped in
  // _sender_op::_op which auto-selects the right specialization:
  // plain callable → start() only; event-dispatch → start() + stop().
  template <typename Receiver>
    requires receiver_of<Receiver, ValueTypes...> &&
      (!std::is_invocable_v<
          _start_cpo::_fn,
          decltype(UNIFEX_DECLVAL(Fn)(UNIFEX_DECLVAL(Receiver)))&>)
  friend auto
  tag_invoke(tag_t<connect>, _sender&& self, Receiver&& rec) noexcept(
      std::is_nothrow_invocable_v<Fn, Receiver&&>) {
    using state_t = decltype(std::move(self.fn_)(std::forward<Receiver>(rec)));
    return _lambda_op::_op<state_t>{
        std::move(self.fn_)(std::forward<Receiver>(rec))};
  }

  UNIFEX_NO_UNIQUE_ADDRESS Fn fn_;
};

template <typename... ValueTypes>
struct _fn {
  template <
      typename Fn,
      typename Tr = std::remove_cv_t<
          decltype(with_sender_traits<_make_traits::sender_traits_literal{}>)>>
    requires move_constructible<Fn>
  _sender<Tr, Fn, ValueTypes...> operator()(Fn && fn, Tr = {}) const noexcept(
      std::is_nothrow_constructible_v<_sender<Tr, Fn, ValueTypes...>, Fn>) {
    return _sender<Tr, Fn, ValueTypes...>{std::forward<Fn>(fn)};
  }
};

}  // namespace _create_raw_sndr

template <typename... ValueTypes>
inline constexpr _create_raw_sndr::_fn<ValueTypes...> create_raw_sender{};
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
