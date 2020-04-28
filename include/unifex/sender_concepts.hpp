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
#include <unifex/detail/unifex_fwd.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/receiver_concepts.hpp>

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {

struct sender_base {};

template <typename>
struct sender_traits;

/// \cond
namespace detail {
  template <template <template <typename...> class, template <typename...> class> class>
  struct _has_value_types;

  template <template <template <typename...> class> class>
  struct _has_error_types;

  template <typename, typename>
  struct _as_receiver;

  template <typename R, typename E>
  struct _as_invocable {
    R* r_ ;
    explicit _as_invocable(R& r) noexcept
      : r_(std::addressof(r)) {}
    _as_invocable(_as_invocable&& other) noexcept
      : r_(std::exchange(other.r_, nullptr)) {}
    ~_as_invocable() {
      if(r_)
        unifex::set_done((R&&) *r_);
    }
    void operator()() & noexcept try {
      unifex::set_value((R&&) *r_);
      r_ = nullptr;
    } catch(...) {
      unifex::set_error((R&&) *r_, std::current_exception());
      r_ = nullptr;
    }
  };

  template <typename S>
  UNIFEX_CONCEPT_FRAGMENT(  //
    _has_sender_types_impl, //
      requires() (          //
        // BUGBUG TODO:
        // typename (std::bool_constant<S::sends_done>),
        typename (_has_value_types<S::template value_types>),
        typename (_has_error_types<S::template error_types>)
      ));
  template <typename S>
  UNIFEX_CONCEPT        //
    _has_sender_types = //
      UNIFEX_FRAGMENT(detail::_has_sender_types_impl, S);

  template <typename S>
  UNIFEX_CONCEPT_FRAGMENT(  //
    _not_has_sender_traits, //
      requires() (          //
        typename (typename sender_traits<S>::_unspecialized)
      ));
  template <typename S>
  UNIFEX_CONCEPT         //
    _has_sender_traits = //
      !UNIFEX_FRAGMENT(detail::_not_has_sender_traits, S);

  struct _void_sender_traits {
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;
  };

  template <typename S>
  struct _typed_sender_traits {
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = typename S::template value_types<Variant, Tuple>;

    template <template <typename...> class Variant>
    using error_types = typename S::template error_types<Variant>;

    // TODO
    // static constexpr bool sends_done = S::sends_done;
  };

  struct _no_sender_traits {
    using _unspecialized = void;
  };

  template <typename S>
  constexpr auto _select_sender_traits() noexcept {
    if constexpr (_has_sender_types<S>) {
      return _typed_sender_traits<S>{};
    } else if constexpr (_is_executor<S>) {
      return _void_sender_traits{};
    } else if constexpr (std::is_base_of_v<sender_base, S>) {
      return sender_base{};
    } else {
      return _no_sender_traits{};
    }
  }
} // namespace detail

template <typename S>
struct sender_traits : decltype(detail::_select_sender_traits<S>()) {};

template <typename S>
UNIFEX_CONCEPT //
  sender =     //
    move_constructible<remove_cvref_t<S>> &&
    detail::_has_sender_traits<remove_cvref_t<S>>;

template <typename S>
UNIFEX_CONCEPT   //
  typed_sender = //
    sender<S> && //
    detail::_has_sender_types<sender_traits<remove_cvref_t<S>>>;

namespace _start_cpo {
  inline const struct _fn {
    template(typename Operation)
      (requires tag_invocable<_fn, Operation&>)
    auto operator()(Operation& op) const noexcept
        -> tag_invoke_result_t<_fn, Operation&> {
      static_assert(
          is_nothrow_tag_invocable_v<_fn, Operation&>,
          "start() customisation must be noexcept");
      return unifex::tag_invoke(_fn{}, op);
    }
    template(typename Operation)
      (requires (!tag_invocable<_fn, Operation&>))
    auto operator()(Operation& op) const noexcept -> decltype(op.start()) {
      static_assert(
          noexcept(op.start()),
          "start() customisation must be noexcept");
      return op.start();
    }
  } start{};
} // namespace _start
using _start_cpo::start;

namespace _connect_cpo {
  using detail::_can_execute;

  template <typename Sender, typename Receiver>
  using _member_connect_result_t =
      decltype((UNIFEX_DECLVAL(Sender&&)).connect(
          UNIFEX_DECLVAL(Receiver&&)));
  template <typename Sender, typename Receiver>
  UNIFEX_CONCEPT_FRAGMENT( //
    _has_member_connect_,  //
      requires() (         //
        typename(_member_connect_result_t<Sender, Receiver>)
      ));
  template <typename Sender, typename Receiver>
  UNIFEX_CONCEPT //
    _with_member_connect = //
      sender<Sender> &&
      UNIFEX_FRAGMENT(_connect_cpo::_has_member_connect_, Sender, Receiver);
  template <typename Sender, typename Receiver>
  UNIFEX_CONCEPT //
    _with_tag_invoke = //
      sender<Sender> && tag_invocable<_fn, Sender, Receiver>;
  template <typename Executor, typename Receiver>
  UNIFEX_CONCEPT //
    _with_execute = //
      receiver_of<Receiver> && _can_execute<Executor, Receiver>;

  inline const struct _fn {
   private:
    template <typename Executor, typename Receiver>
    struct _as_op {
      struct type {
        remove_cvref_t<Executor> e_;
        remove_cvref_t<Receiver> r_;
        void start() noexcept try {
          using _as_invocable =
            detail::_as_invocable<remove_cvref_t<Receiver>, Executor>;
          unifex::execute(std::move(e_), _as_invocable{r_});
        } catch(...) {
          // BUGBUG: see https://github.com/executors/executors/issues/463
          // unifex::set_error(std::move(r_), std::current_exception());
        }
      };
    };
    template <typename Executor, typename Receiver>
    using _as_operation = typename _as_op<Executor, Receiver>::type;
    template <typename Sender, typename Receiver>
    static auto _select() {
      if constexpr (_with_tag_invoke<Sender, Receiver>) {
        return meta_tag_invoke_result<_fn>{};
      } else if constexpr (_with_member_connect<Sender, Receiver>) {
        return meta_quote2<_member_connect_result_t>{};
      } else if constexpr (_with_execute<Sender, Receiver>) {
        return meta_quote2<_as_operation>{};
      } else {
        return type_always<void>{};
      }
    }
    template <typename Sender, typename Receiver>
    using _result_t = typename decltype(_fn::_select<Sender, Receiver>())
        ::template apply<Sender, Receiver>;
   public:
    template(typename Sender, typename Receiver)
      (requires receiver<Receiver> AND
          _with_tag_invoke<Sender, Receiver>)
    auto operator()(Sender&& s, Receiver&& r) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Receiver>) ->
        _result_t<Sender, Receiver> {
      return unifex::tag_invoke(_fn{}, (Sender &&) s, (Receiver &&) r);
    }
    template(typename Sender, typename Receiver)
      (requires receiver<Receiver> AND
          (!_with_tag_invoke<Sender, Receiver>) AND
          _with_member_connect<Sender, Receiver>)
    auto operator()(Sender&& s, Receiver&& r) const
        noexcept(noexcept(((Sender &&) s).connect((Receiver &&) r))) ->
        _result_t<Sender, Receiver> {
      return ((Sender &&) s).connect((Receiver &&) r);
    }
    template(typename Executor, typename Receiver)
      (requires receiver<Receiver> AND
          (!_with_tag_invoke<Executor, Receiver>) AND
          (!_with_member_connect<Executor, Receiver>) AND
          _with_execute<Executor, Receiver>)
    auto operator()(Executor&& e, Receiver&& r) const
        noexcept(std::is_nothrow_constructible_v<
          _as_operation<Executor, Receiver>, Executor, Receiver>) ->
        _result_t<Executor, Receiver> {
      return _as_operation<Executor, Receiver>{(Executor &&) e, (Receiver &&) r};
    }
  } connect{};
} // namespace _connect_cpo
using _connect_cpo::connect;

#if UNIFEX_CXX_CONCEPTS
// Define the sender_to concept without macros for
// improved diagnostics:
template <typename Sender, typename Receiver>
concept //
  sender_to = //
    sender<Sender> &&
    receiver<Receiver> &&
    requires (Sender&& s, Receiver&& r) {
      connect((Sender&&) s, (Receiver&&) r);
    };
#else
template <typename Sender, typename Receiver>
UNIFEX_CONCEPT_FRAGMENT( //
  _sender_to, //
    requires (Sender&& s, Receiver&& r) ( //
      connect((Sender&&) s, (Receiver&&) r)
    ));
template <typename Sender, typename Receiver>
UNIFEX_CONCEPT //
  sender_to =
    sender<Sender> &&
    receiver<Receiver> &&
    UNIFEX_FRAGMENT(_sender_to, Sender, Receiver);
#endif

template <typename Sender, typename Receiver>
using connect_result_t =
  decltype(connect(UNIFEX_DECLVAL(Sender), UNIFEX_DECLVAL(Receiver)));

/// \cond
template <typename Sender, typename Receiver>
using operation_t [[deprecated("Use connect_result_t instead of operation_t")]] =
    connect_result_t<Sender, Receiver>;
/// \endcond

template <typename Sender, typename Receiver>
[[deprecated("Use sender_to instead of is_connectable_v")]]
inline constexpr bool is_connectable_v =
  is_callable_v<decltype(connect), Sender, Receiver>;

template <typename Sender, typename Receiver>
using is_connectable [[deprecated]] =
  is_callable<decltype(connect), Sender, Receiver>;

template <typename Sender, typename Receiver>
inline constexpr bool is_nothrow_connectable_v =
  is_nothrow_callable_v<decltype(connect), Sender, Receiver>;

template <typename Sender, typename Receiver>
using is_nothrow_connectable = is_nothrow_callable<decltype(connect), Sender, Receiver>;

/// \cond
namespace detail {
  template <typename S, typename F, typename>
  inline constexpr bool _can_submit =
    sender_to<S, _as_receiver<remove_cvref_t<F>, S>>;
#if UNIFEX_CXX_CONCEPTS
  template <typename S1, typename R, typename S2>
    requires same_as<remove_cvref_t<S1>, remove_cvref_t<S2>>
  inline constexpr bool _can_submit<S1, _as_invocable<R, S2>> = false;
#else
  template <typename S1, typename R, typename S2>
  inline constexpr bool _can_submit<
      S1,
      _as_invocable<R, S2>,
      std::enable_if_t<UNIFEX_IS_SAME(remove_cvref_t<S1>, remove_cvref_t<S2>)>> =
    false;
#endif
} // namespace detail
/// \endcond

template <typename Sender, typename Adaptor>
using adapt_error_types_t =
    typename Sender::template error_types<Adaptor::template apply>;

template <
    typename Sender,
    template <typename...> class Variant,
    typename Adaptor>
using adapt_value_types_t =
    typename Sender::template value_types<Variant, typename Adaptor::apply>;

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

template <typename Sender>
using single_value_result_t = non_void_t<wrap_reference_t<decay_rvalue_t<
    typename Sender::template value_types<single_type, single_value_type>::
        type::type>>>;

template <typename Sender>
constexpr bool is_sender_nofail_v =
    Sender::template error_types<is_empty_list>::value;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
#include <unifex/executor_concepts.hpp>
