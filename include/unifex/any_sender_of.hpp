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

#include <unifex/any_unique.hpp>
#include <unifex/sender_concepts.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

template <typename... Values>
using any_receiver_of =
    any_unique_t<
        overload<void(this_&&, Values...)>(set_value),
        overload<void(this_&&, std::exception_ptr) noexcept>(set_error),
        overload<void(this_&&) noexcept>(set_done)>;

using any_operation_state =
    any_unique_t<
        overload<void(this_&) noexcept>(start)>;

namespace _any {

// For in-place constructing non-movable operation states.
// Relies on C++17's guaranteed copy elision.
template <typename Fun>
struct _rvo {
  Fun fun_;
  operator callable_result_t<Fun>() && {
    return ((Fun&&) fun_)();
  }
};

template <typename Fun>
_rvo(Fun) -> _rvo<Fun>;

template <typename... Values>
struct _connect_fn {
  using type_erased_signature_t =
      any_operation_state(this_&&, any_receiver_of<Values...>&&);

  template(typename Sender)
      (requires sender_to<Sender, any_receiver_of<Values...>>)
  friend any_operation_state
  tag_invoke(_connect_fn, Sender&& s, any_receiver_of<Values...>&& r) {
    return any_operation_state{
      std::in_place_type<connect_result_t<Sender, any_receiver_of<Values...>>>,
      _rvo{[&]() { return connect((Sender&&) s, std::move(r)); }}
    };
  }

  template(typename Self)
      (requires tag_invocable<_connect_fn, Self, any_receiver_of<Values...>>)
  any_operation_state operator()(Self&& s, any_receiver_of<Values...>&& r) const {
    return tag_invoke(*this, (Self&&) s, std::move(r));
  }
};

template <typename... Values>
inline constexpr _connect_fn<Values...> _any_connect{};

template <typename... Values>
using _sender_base = any_unique_t<_any_connect<Values...>>;

template <typename... Values>
struct _sender {
  struct type;
};

template <typename... Values>
struct _sender<Values...>::type : private _sender_base<Values...> {
  template <template <class...> class Variant, template <class...> class Tuple>
  using value_types = Variant<Tuple<Values...>>;

  template <template <class...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  using _sender_base<Values...>::_sender_base;

  any_operation_state connect(any_receiver_of<Values...> receiver) && {
    return _any::_any_connect<Values...>(
        static_cast<_sender_base<Values...>&&>(*this),
        std::move(receiver));
  }
};

} // namespace _any

template <typename... Values>
using any_sender_of = typename _any::_sender<Values...>::type;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
