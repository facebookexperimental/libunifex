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

#include <unifex/any_unique.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/type_list.hpp>
#include <unifex/with_query_value.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

// Forward-declaration for any_scheduler, defined in
// <any_scheduler.hpp>
namespace _any_sched {

template <typename... CPOs>
struct _with {
  struct any_scheduler;
  struct any_scheduler_ref;
};

template <typename... CPOs>
using any_scheduler = typename _with<CPOs...>::any_scheduler;

template <typename... CPOs>
using any_scheduler_ref = typename _with<CPOs...>::any_scheduler_ref;

} // _any_sched

namespace _any {

using _operation_state =
    any_unique_t<overload<void(this_&) noexcept>(start)>;

template <typename CPOs>
struct _rec_ref_base;

template <typename... CPOs>
struct _rec_ref_base<type_list<CPOs...>> {
#if defined(_MSC_VER)
  template <typename... Values>
  using type =
      any_ref<
          tag_t<overload(set_value, sig<void(this_&&, Values...)>)>,
          tag_t<overload(set_error, sig<void(this_&&, std::exception_ptr) noexcept>)>,
          tag_t<overload(set_done, sig<void(this_&&) noexcept>)>,
          CPOs...>;
#else
  template <typename... Values>
  using type =
      any_ref<
          tag_t<overload<void(this_&&, Values...)>(set_value)>,
          tag_t<overload<void(this_&&, std::exception_ptr) noexcept>(set_error)>,
          tag_t<overload<void(this_&&) noexcept>(set_done)>,
          CPOs...>;
#endif
};

template <typename CPOs, typename... Values>
struct _rec_ref {
  struct type;
};

template <typename CPOs, typename... Values>
struct _rec_ref<CPOs, Values...>::type
    : _rec_ref_base<CPOs>::template type<Values...> {
  template <typename Op>
  type(inplace_stop_token st, Op* op)
    : _rec_ref_base<CPOs>::template type<Values...>(*op)
    , stoken_(st) {}

private:
  friend inplace_stop_token tag_invoke(tag_t<get_stop_token>, const type& self) noexcept {
    return self.stoken_;
  }

  inplace_stop_token stoken_;
};

template <typename CPOs, typename... Values>
using _receiver_ref = typename _rec_ref<CPOs, Values...>::type;

// For in-place constructing non-movable operation states.
// Relies on C++17's guaranteed copy elision.
template <typename Sender, typename Receiver>
struct _rvo {
  Sender&& s;
  Receiver r;
  operator connect_result_t<Sender, Receiver>() {
    return connect((Sender &&) s, std::move(r));
  }
};
template <typename Sender, typename Receiver>
_rvo(Sender&&, Receiver) -> _rvo<Sender, Receiver>;

template <typename CPOs, typename... Values>
struct _connect_fn {
  struct type;
};

template <typename CPOs, typename... Values>
struct _connect_fn<CPOs, Values...>::type {
  using _rec_ref_t = _receiver_ref<CPOs, Values...>;
  using type_erased_signature_t = _operation_state(this_&&, _rec_ref_t);

  template(typename Sender)
    (requires sender_to<Sender, _rec_ref_t>)
  friend _operation_state
  tag_invoke(const type&, Sender&& s, _rec_ref_t r) {
    using Op = connect_result_t<Sender, _rec_ref_t>;
    return _operation_state{std::in_place_type<Op>, _rvo{(Sender &&) s, std::move(r)}};
  }

#ifdef _MSC_VER
  // MSVC (_MSC_VER == 1927) doesn't seem to like the requires
  // clause here. Use SFINAE instead.
  template <typename Self>
  tag_invoke_result_t<type, Self, _rec_ref_t>
  operator()(Self&& s, _rec_ref_t r) const {
    return tag_invoke(*this, (Self&&) s, std::move(r));
  }
#else
  template(typename Self)
    (requires tag_invocable<type, Self, _rec_ref_t>)
  _operation_state operator()(Self&& s, _rec_ref_t r) const {
    return tag_invoke(*this, (Self&&) s, std::move(r));
  }
#endif
};

template <typename CPOs, typename... Values>
inline constexpr typename _connect_fn<CPOs, Values...>::type _connect{};

template <typename Receiver>
struct _op_for {
  struct type;
};

template <typename Receiver>
using _operation_state_for = typename _op_for<Receiver>::type;

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

  // This operation state also implements the receiver CPOs and forwards them
  // to the receiver after unsubscribing the stop token.
  template (typename CPO, typename... Args)
    (requires is_receiver_cpo_v<CPO> AND is_callable_v<CPO, Receiver, Args...>)
  friend void tag_invoke(CPO cpo, type&& self, Args&&... args)
    noexcept(is_nothrow_callable_v<CPO, Receiver, Args...>) {
    self.subscription_.unsubscribe();
    cpo(std::move(self).rec_, (Args&&) args...);
  }

  // Forward other receiver queries
  template (typename CPO)
    (requires is_receiver_query_cpo_v<CPO> AND is_callable_v<CPO, const Receiver&>)
  friend auto tag_invoke(CPO cpo, const type& self)
    noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
    -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(self.rec_);
  }

  UNIFEX_NO_UNIQUE_ADDRESS
  Receiver rec_;
  detail::inplace_stop_token_adapter_subscription<stop_token_type_t<Receiver>> subscription_{};
  _operation_state state_;
};

template <typename CPOs, typename... Values>
using _sender_base = any_unique_t<_connect<CPOs, Values...>>;

template <typename... Values>
struct _sender {
  struct type;
};

template <typename... CPOs>
struct _with {
  template <typename... Values>
  struct _sender {
    struct type;
  };

  template <typename... Values>
  using any_sender_of = typename _sender<Values...>::type;

  using any_scheduler = _any_sched::any_scheduler<CPOs...>;

  using any_scheduler_ref = _any_sched::any_scheduler_ref<CPOs...>;

  template <typename... Values>
  using any_receiver_ref = _receiver_ref<type_list<CPOs...>, Values...>;
};

template <typename... CPOs>
template <typename... Values>
struct _with<CPOs...>::_sender<Values...>::type
    : private _sender_base<type_list<CPOs...>, Values...> {
  template <template <class...> class Variant, template <class...> class Tuple>
  using value_types = Variant<Tuple<Values...>>;

  template <template <class...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  template (typename Receiver)
    (requires receiver_of<Receiver, Values...> AND
      (invocable<CPOs, Receiver const&> &&...))
  _operation_state_for<Receiver> connect(Receiver r) && {
    any_unique_t<_connect<type_list<CPOs...>, Values...>>& self = *this;
    return _operation_state_for<Receiver>{
        std::move(r),
        [&self](_receiver_ref<type_list<CPOs...>, Values...> rec) {
          return _connect<type_list<CPOs...>, Values...>(std::move(self), std::move(rec));
        }
      };
  }

  using _sender_base<type_list<CPOs...>, Values...>::_sender_base;
  UNIFEX_ALWAYS_INLINE ~type() = default;
  type(type&&) = default;
};

template <typename... Values>
struct _sender<Values...>::type : _with<>::_sender<Values...>::type {
  using _with<>::_sender<Values...>::type::type;
};

} // namespace _any

template <typename Receiver>
using any_operation_state_for = _any::_operation_state_for<Receiver>;

template <typename... Values>
using any_sender_of = typename _any::_sender<Values...>::type;

template <typename... Values>
using any_receiver_ref = _any::_receiver_ref<type_list<>, Values...>;

template <auto&... CPOs>
using with_receiver_queries = _any::_with<tag_t<CPOs>...>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
