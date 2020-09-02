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

#if defined(_MSC_VER)
template <typename... Values>
using any_receiver_of =
    any_unique<
        overload_t<set_value, void(this_&&, Values...)>,
        overload_t<set_error, void(this_&&, std::exception_ptr) noexcept>,
        overload_t<set_done, void(this_&&) noexcept>>;
#else
template <typename... Values>
using any_receiver_of =
    any_unique_t<
        overload<void(this_&&, Values...)>(set_value),
        overload<void(this_&&, std::exception_ptr) noexcept>(set_error),
        overload<void(this_&&) noexcept>(set_done)>;
#endif

using any_operation_state =
    any_unique_t<
        overload<void(this_&) noexcept>(start)>;

namespace _any {

template <typename... Values>
struct _rec_ref {
  struct type;
};

template <typename... Values>
struct _rec_ref<Values...>::type {
  template (typename Receiver)
    (requires (!same_as<Receiver const, type const>) AND
        receiver_of<Receiver, Values...>)
  type(Receiver& rec)
    : rec_(std::addressof(rec))
    , set_value_fn_([](void* rec, Values&&... values) {
        unifex::set_value(
            static_cast<Receiver&&>(*static_cast<Receiver*>(rec)),
            (Values&&) values...);
      })
    , set_error_fn_([](void* rec, std::exception_ptr e) noexcept {
        unifex::set_error(
            static_cast<Receiver&&>(*static_cast<Receiver*>(rec)),
            (std::exception_ptr&&) e);
      })
    , set_done_fn_([](void* rec) noexcept {
        unifex::set_done(
            static_cast<Receiver&&>(*static_cast<Receiver*>(rec)));
      })
  {}

  void set_value(Values... values) && {
    set_value_fn_(rec_, (Values&&) values...);
  }
  void set_error(std::exception_ptr e) && noexcept {
    set_error_fn_(rec_, (std::exception_ptr&&) e);
  }
  void set_done() && noexcept {
    set_done_fn_(rec_);
  }

private:
  void *rec_;
  void (*set_value_fn_)(void*, Values&&...);
  void (*set_error_fn_)(void*, std::exception_ptr) noexcept;
  void (*set_done_fn_)(void*) noexcept;
};

template <typename... Values>
using _receiver_ref = typename _rec_ref<Values...>::type;

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
struct _connect_fn_impl {
  struct type;
};

template <typename... Values>
struct _connect_fn_impl<Values...>::type {
  using type_erased_signature_t =
      any_operation_state(this_&&, _receiver_ref<Values...>);

  template(typename Sender)
      (requires sender_to<Sender, _receiver_ref<Values...>>)
  friend any_operation_state
  tag_invoke(type, Sender&& s, _receiver_ref<Values...> r) {
    return any_operation_state{
      std::in_place_type<connect_result_t<Sender, _receiver_ref<Values...>>>,
      _rvo{[r, &s]() { return connect((Sender&&) s, std::move(r)); }}
    };
  }

  template(typename Self)
      (requires tag_invocable<type, Self, _receiver_ref<Values...>>)
  any_operation_state operator()(Self&& s, _receiver_ref<Values...> r) const {
    return tag_invoke(*this, (Self&&) s, std::move(r));
  }
};

template <typename... Values>
using _connect_fn = typename _connect_fn_impl<Values...>::type;

template <typename... Values>
inline constexpr _connect_fn<Values...> _connect{};

template <typename Receiver>
struct _op_for {
  struct type;
};

template <typename Receiver>
struct _op_for<Receiver>::type {
  template <typename Fn>
  explicit type(Receiver r, Fn fn)
    : rec_((Receiver&&) r)
    , state_{fn(rec_)}
  {}

  void start() & noexcept {
    unifex::start(state_);
  }
private:
  Receiver rec_;
  any_operation_state state_;
};

template <typename... Values>
using _sender_base = any_unique_t<_connect<Values...>>;

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

  template <typename Receiver>
  typename _op_for<Receiver>::type connect(Receiver r) && {
    any_unique_t<_connect<Values...>>& self = *this;
    return typename _op_for<Receiver>::type{
        std::move(r),
        [&self](_receiver_ref<Values...> rec) {
          return _connect<Values...>(std::move(self), std::move(rec));
        }
      };
  }

  using _sender_base<Values...>::_sender_base;
};

} // namespace _any

template <typename... Values>
using any_receiver_ref_of = typename _any::_receiver_ref<Values...>;

template <typename Receiver>
using any_operation_state_for = typename _any::_op_for<Receiver>::type;

template <typename... Values>
using any_sender_of = typename _any::_sender<Values...>::type;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
