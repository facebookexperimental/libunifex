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
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _any {

using _operation_state =
    any_unique_t<overload<void(this_&) noexcept>(start)>;

template <typename... Values>
struct _rec_ref {
  struct type;
};

template <typename... Values>
struct _rec_ref<Values...>::type {
  template <typename Op>
  type(inplace_stop_token st, Op* op)
    : op_(op)
    , st_(st)
    , set_value_fn_([](void* op, Values&&... values) {
        static_cast<Op*>(op)->subscription_.unsubscribe();
        unifex::set_value(
            std::move(static_cast<Op*>(op)->rec_),
            (Values&&) values...);
      })
    , set_error_fn_([](void* op, std::exception_ptr e) noexcept {
        static_cast<Op*>(op)->subscription_.unsubscribe();
        unifex::set_error(
            std::move(static_cast<Op*>(op)->rec_),
            (std::exception_ptr&&) e);
      })
    , set_done_fn_([](void* op) noexcept {
        static_cast<Op*>(op)->subscription_.unsubscribe();
        unifex::set_done(
            std::move(static_cast<Op*>(op)->rec_));
      })
  {}

  void set_value(Values&&... values) && {
    set_value_fn_(op_, (Values&&) values...);
  }
  void set_error(std::exception_ptr e) && noexcept {
    set_error_fn_(op_, (std::exception_ptr&&) e);
  }
  void set_done() && noexcept {
    set_done_fn_(op_);
  }

private:
  friend inplace_stop_token tag_invoke(tag_t<get_stop_token>, const type& self) {
    return self.st_;
  }

  void *op_;
  inplace_stop_token st_;
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
struct _connect_fn {
  struct type;
};

template <typename... Values>
struct _connect_fn<Values...>::type {
  using type_erased_signature_t =
      _operation_state(this_&&, _receiver_ref<Values...>);

  template(typename Sender)
      (requires sender_to<Sender, _receiver_ref<Values...>>)
  friend _operation_state
  tag_invoke(type, Sender&& s, _receiver_ref<Values...> r) {
    return _operation_state{
      std::in_place_type<connect_result_t<Sender, _receiver_ref<Values...>>>,
      _rvo{[r, &s]() { return connect((Sender&&) s, std::move(r)); }}
    };
  }

  template(typename Self)
      (requires tag_invocable<type, Self, _receiver_ref<Values...>>)
  _operation_state operator()(Self&& s, _receiver_ref<Values...> r) const {
    return tag_invoke(*this, (Self&&) s, std::move(r));
  }
};

template <typename... Values>
inline constexpr typename _connect_fn<Values...>::type _connect{};

template <typename StopToken>
struct inplace_stop_token_adapter_subscription {
  inplace_stop_token subscribe(StopToken stoken) noexcept {
    isSubscribed_ = true;
    return stopTokenAdapter_.subscribe(std::move(stoken));
  }
  void unsubscribe() noexcept {
    if (isSubscribed_) {
      isSubscribed_ = false;
      stopTokenAdapter_.unsubscribe();
    }
  }
  ~inplace_stop_token_adapter_subscription() {
    unsubscribe();
  }
private:
  bool isSubscribed_ = false;
  UNIFEX_NO_UNIQUE_ADDRESS
  inplace_stop_token_adapter<StopToken> stopTokenAdapter_{};
};

template <typename Receiver>
struct _op_for {
  struct type;
};

template <typename Receiver>
struct _op_for<Receiver>::type {
  template <typename Fn>
  explicit type(Receiver r, Fn fn)
    : rec_((Receiver&&) r)
    , state_{fn({subscription_.subscribe(unifex::get_stop_token(rec_)), this})}
  {}

  void start() & noexcept {
    unifex::start(state_);
  }

  UNIFEX_NO_UNIQUE_ADDRESS
  Receiver rec_;
  inplace_stop_token_adapter_subscription<stop_token_type_t<Receiver>> subscription_{};
  _operation_state state_;
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

  template (typename Receiver)
    (requires receiver_of<Receiver, Values...>)
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

template <typename Receiver>
using any_operation_state_for = typename _any::_op_for<Receiver>::type;

template <typename... Values>
using any_sender_of = typename _any::_sender<Values...>::type;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
