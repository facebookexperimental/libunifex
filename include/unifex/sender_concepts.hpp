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

#include <unifex/blocking.hpp>
#include <unifex/detail/unifex_fwd.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/tracing/async_stack.hpp>
#include <unifex/tracing/get_async_stack_frame.hpp>
#include <unifex/tracing/get_return_address.hpp>
#include <unifex/tracing/inject_async_stack.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>

#if !UNIFEX_NO_COROUTINES
#  include <unifex/coroutine_concepts.hpp>
#endif

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {

template <typename>
struct sender_traits;

/// \cond
namespace detail {
using unifex::_block::_has_blocking;

template <template <template <typename...> class, template <typename...> class>
          class>
struct _has_value_types;

template <template <template <typename...> class> class>
struct _has_error_types;

template <typename Sender, bool = _has_blocking<Sender>::value>
struct _blocking : blocking_kind::constant<blocking_kind::maybe> {};

template <typename Sender>
struct _blocking<Sender, true> : blocking_kind::constant<Sender::blocking> {};

template <typename Sender, typename = void>
struct _has_is_always_scheduler_affine : std::false_type {};

template <typename Sender>
struct _has_is_always_scheduler_affine<
    Sender,
    std::void_t<decltype(Sender::is_always_scheduler_affine)>>
  : std::true_type {};

template <
    typename Sender,
    bool = _has_is_always_scheduler_affine<Sender>::value>
struct _is_always_scheduler_affine
  : std::bool_constant<
        blocking_kind::always_inline == _blocking<Sender>::value> {};

template <typename Sender>
struct _is_always_scheduler_affine<Sender, true>
  : std::bool_constant<Sender::is_always_scheduler_affine> {};

template <typename S>
UNIFEX_CONCEPT_FRAGMENT(     //
    _has_sender_types_impl,  //
    requires()(              //
        typename(std::bool_constant<S::sends_done>),
        typename(_has_value_types<S::template value_types>),
        typename(_has_error_types<S::template error_types>)));
template <typename S>
UNIFEX_CONCEPT           //
    _has_sender_types =  //
    UNIFEX_FRAGMENT(detail::_has_sender_types_impl, S);

template <typename S>
UNIFEX_CONCEPT_FRAGMENT(          //
    _has_bulk_sender_types_impl,  //
    requires()(                   //
        typename(std::bool_constant<S::sends_done>),
        typename(_has_value_types<S::template value_types>),
        typename(_has_value_types<S::template next_types>),
        typename(_has_error_types<S::template error_types>)));
template <typename S>
UNIFEX_CONCEPT                //
    _has_bulk_sender_types =  //
    UNIFEX_FRAGMENT(detail::_has_bulk_sender_types_impl, S);

template <typename S>
struct _sender_traits {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = typename S::template value_types<Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename S::template error_types<Variant>;

  static constexpr bool sends_done = S::sends_done;

  static constexpr bool is_always_scheduler_affine =
      _is_always_scheduler_affine<S>::value;

  static constexpr blocking_kind blocking = _blocking<S>::value;
};

template <typename S>
struct _bulk_sender_traits : _sender_traits<S> {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using next_types = typename S::template next_types<Variant, Tuple>;
};

struct _no_sender_traits {
  using _unspecialized = void;
};

// Workaround for unknown MSVC (19.28.29333) bug
#ifdef _MSC_VER
template <typename S>
inline constexpr bool _has_sender_traits =
    !std::is_base_of_v<_no_sender_traits, sender_traits<S>>;
#elif UNIFEX_CXX_CONCEPTS
template <typename S>
concept _has_sender_traits = !requires {
  typename sender_traits<S>::_unspecialized;
};
#else
template <typename S>
UNIFEX_CONCEPT_FRAGMENT(     //
    _not_has_sender_traits,  //
    requires()(              //
        typename(typename sender_traits<S>::_unspecialized)));
template <typename S>
UNIFEX_CONCEPT            //
    _has_sender_traits =  //
    (!UNIFEX_FRAGMENT(detail::_not_has_sender_traits, S));
#endif

template <typename S>
constexpr auto _select_sender_traits() noexcept {
  if constexpr (_has_bulk_sender_types<S>) {
    return _bulk_sender_traits<S>{};
  } else if constexpr (_has_sender_types<S>) {
    return _sender_traits<S>{};
  } else {
    return _no_sender_traits{};
  }
}
}  // namespace detail

template <typename S>
struct sender_traits : decltype(detail::_select_sender_traits<S>()) {};

template <typename S>
UNIFEX_CONCEPT  //
    sender =    //
    move_constructible<remove_cvref_t<S>>&&
        detail::_has_sender_traits<remove_cvref_t<S>>&&  //
            detail::_has_sender_types<sender_traits<remove_cvref_t<S>>>;

template <typename S>
[[deprecated("Use unifex::sender<S> instead")]]  //
inline constexpr bool typed_sender = sender<S>;

template <typename S>
UNIFEX_CONCEPT     //
    bulk_sender =  //
    sender<S>&&    //
        detail::_has_bulk_sender_types<sender_traits<remove_cvref_t<S>>>;

template <typename S>
[[deprecated("Use unifex::bulk_sender<S> instead")]]  //
inline constexpr bool typed_bulk_sender = bulk_sender<S>;

namespace _start_cpo {
struct _fn {
  template(typename Operation)                   //
      (requires tag_invocable<_fn, Operation&>)  //
      auto
      operator()(Operation& op) const noexcept
      -> tag_invoke_result_t<_fn, Operation&> {
    static_assert(
        is_nothrow_tag_invocable_v<_fn, Operation&>,
        "start() customisation must be noexcept");
    return unifex::tag_invoke(_fn{}, op);
  }
  template(typename Operation)                     //
      (requires(!tag_invocable<_fn, Operation&>))  //
      auto
      operator()(Operation& op) const noexcept -> decltype(op.start()) {
    static_assert(
        noexcept(op.start()), "start() customisation must be noexcept");
    return op.start();
  }
};
}  // namespace _start_cpo
inline const _start_cpo::_fn start{};

namespace _connect {

template <typename Sender, typename Receiver>
using _member_connect_result_t =
    decltype((UNIFEX_DECLVAL(Sender&&)).connect(UNIFEX_DECLVAL(Receiver&&)));

template <typename Sender, typename Receiver>
UNIFEX_CONCEPT_FRAGMENT(   //
    _has_member_connect_,  //
    requires()(            //
        typename(_member_connect_result_t<Sender, Receiver>)));

template <typename Sender, typename Receiver>
UNIFEX_CONCEPT                //
    _is_member_connectible =  //
    sender<Sender> &&
    UNIFEX_FRAGMENT(_connect::_has_member_connect_, Sender, Receiver);

template <typename Sender, typename Receiver>
UNIFEX_CONCEPT                        //
    _is_nothrow_member_connectible =  //
    _is_member_connectible<Sender, Receiver> &&
    noexcept(UNIFEX_DECLVAL(Sender&&).connect(UNIFEX_DECLVAL(Receiver&&)));

namespace _cpo {

struct _fn {
private:
  struct _impl {
    template(typename S, typename R)         //
        (requires tag_invocable<_fn, S, R>)  //
        auto
        operator()(S&& s, R&& r) const
        noexcept(is_nothrow_tag_invocable_v<_fn, S, R>)
            -> tag_invoke_result_t<_fn, S, R> {
      return unifex::tag_invoke(_fn{}, std::forward<S>(s), std::forward<R>(r));
    }

    template(typename S, typename R)  //
        (requires(!tag_invocable<_fn, S, R>)
             AND _is_member_connectible<S, R>)  //
        auto
        operator()(S&& s, R&& r) const
        noexcept(_is_nothrow_member_connectible<S, R>)
            -> _member_connect_result_t<S, R> {
      return std::forward<S>(s).connect(std::forward<R>(r));
    }
  };

  template <typename S, typename R>
  using op_t = _inject::
      op_wrapper<std::invoke_result_t<_impl, S, _inject::receiver_t<R>>, R>;

public:
  template(typename S, typename R)  //
      (requires sender<S> AND receiver<R>) auto
      operator()(S&& s, R&& r) const noexcept(noexcept(_inject::make_op_wrapper(
          std::forward<S>(s), std::forward<R>(r), _impl{}))) -> op_t<S, R> {
    return _inject::make_op_wrapper(
        std::forward<S>(s), std::forward<R>(r), _impl{});
  }
};

}  // namespace _cpo
}  // namespace _connect
inline const _connect::_cpo::_fn connect{};

#if UNIFEX_CXX_CONCEPTS
// Define the sender_to concept without macros for
// improved diagnostics:
template <typename Sender, typename Receiver>
concept          //
    sender_to =  //
    sender<Sender> && receiver<Receiver> && requires(Sender&& s, Receiver&& r) {
  connect((Sender &&) s, (Receiver &&) r);
};
#else
template <typename Sender, typename Receiver>
UNIFEX_CONCEPT_FRAGMENT(                 //
    _sender_to,                          //
    requires(Sender&& s, Receiver&& r)(  //
        connect((Sender &&) s, (Receiver &&) r)));
template <typename Sender, typename Receiver>
UNIFEX_CONCEPT  //
    sender_to = sender<Sender>&& receiver<Receiver>&&
        UNIFEX_FRAGMENT(_sender_to, Sender, Receiver);
#endif

template <typename Sender, typename Receiver>
using connect_result_t =
    decltype(connect(UNIFEX_DECLVAL(Sender), UNIFEX_DECLVAL(Receiver)));

template <typename Sender, typename Receiver>
inline constexpr bool is_nothrow_connectable_v =
    std::is_nothrow_invocable_v<tag_t<connect>, Sender, Receiver>;

template <typename Sender, typename Receiver>
using is_nothrow_connectable =
    std::is_nothrow_invocable<tag_t<connect>, Sender, Receiver>;

template <
    typename Sender,
    template <typename...>
    class Variant,
    template <typename...>
    class Tuple>
using sender_value_types_t =
    typename sender_traits<Sender>::template value_types<Variant, Tuple>;

template <typename Sender, template <typename...> class Variant>
using sender_error_types_t =
    typename sender_traits<Sender>::template error_types<Variant>;

template <typename... Types>
struct single_value_type {
  using type = std::tuple<Types...>;
};

template <typename T>
struct single_value_type<T> {
  using type = T;
};

template <>
struct single_value_type<> {
  using type = void;
};

template <typename... Overloads>
struct single_overload {};

template <typename Overload>
struct single_overload<Overload> {
  using type = Overload;
};

template <>
struct single_overload<> {
private:
  struct impl {
    using type = void;
  };

public:
  using type = impl;
};

template <typename Sender>
using sender_single_value_return_type_t =
    typename sender_value_types_t<Sender, single_overload, single_value_type>::
        type::type;

template <typename Sender>
using sender_single_value_result_t = non_void_t<wrap_reference_t<
    decay_rvalue_t<sender_single_value_return_type_t<Sender>>>>;

template <typename Sender>
constexpr bool is_sender_nofail_v =
    sender_error_types_t<Sender, is_empty_list>::value;

template <typename Sender>
using sender_value_type_list_t =
    sender_value_types_t<Sender, type_list, type_list>;

template <typename Sender>
using sender_error_type_list_t = sender_error_types_t<Sender, type_list>;

/// \cond
namespace _detail {
template <typename... Types>
using _is_single_valued_tuple = std::bool_constant<1 >= sizeof...(Types)>;

template <typename... Types>
using _is_single_valued_variant =
    std::bool_constant<sizeof...(Types) == 1 && (Types::value && ...)>;
}  // namespace _detail
/// \endcond

template <typename Sender>
UNIFEX_CONCEPT_FRAGMENT(  //
    _single_sender_impl,  //
    requires()(0) &&      //
        sender_traits<remove_cvref_t<Sender>>::template value_types<
            _detail::_is_single_valued_variant,
            _detail::_is_single_valued_tuple>::value);

template <typename Sender>
UNIFEX_CONCEPT _single_sender =  //
    sender<Sender>&& UNIFEX_FRAGMENT(unifex::_single_sender_impl, Sender);

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
