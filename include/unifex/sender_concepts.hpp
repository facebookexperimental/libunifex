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

#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/receiver_concepts.hpp>

#include <exception>
#include <tuple>
#include <type_traits>

namespace unifex {

struct sender_base {};

template <typename>
struct sender_traits;

/// \cond
namespace detail {
  template <typename, typename>
  struct _as_invocable;

  template <typename, typename>
  struct _as_receiver;

  template <typename, typename = void>
  inline constexpr bool _is_executor = false;

  template <template <template <typename...> class, template <typename...> class> class>
  struct _has_value_types;

  template <template <template <typename...> class> class>
  struct _has_error_types;

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
    template <template <typename...> class Tuple, template <typename...> class Variant>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;
  };

  template <typename S>
  struct _typed_sender_traits {
    template <template <typename...> class Tuple, template <typename...> class Variant>
    using value_types = typename S::template value_types<Tuple, Variant>;

    template <template <typename...> class Variant>
    using error_types = typename S::template error_types<Variant>;

    static constexpr bool sends_done = S::sends_done;
  };

  struct _void_receiver {
    void set_value() noexcept;
    void set_error(std::exception_ptr) noexcept;
    void set_done() noexcept;
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
    } else if constexpr (derived_from<S, sender_base>) {
      return sender_base{};
    } else {
      return _no_sender_traits{};
    }
  }
} // namespace detail
/// \endcond

template <typename S>
struct sender_traits : decltype(detail::_select_sender_traits<S>()) {};

template <typename S>
UNIFEX_CONCEPT //
  sender =     //
    move_constructible<std::remove_cvref_t<S>> &&
    detail::_has_sender_traits<std::remove_cvref_t<S>>;

template <typename S>
UNIFEX_CONCEPT   //
  typed_sender = //
    sender<S> && //
    detail::_has_sender_types<sender_traits<std::remove_cvref_t<S>>>;

namespace _start {
  inline constexpr struct _fn {
   private:
    template <bool>
    struct _impl {
      template <typename Operation>
      auto operator()(Operation& op) const noexcept
          -> tag_invoke_result_t<_fn, Operation&> {
        static_assert(
            is_nothrow_tag_invocable_v<_fn, Operation&>,
            "start() customisation must be noexcept");
        return unifex::tag_invoke(_fn{}, op);
      }
    };
   public:
    template <typename Operation>
    auto operator()(Operation& op) const noexcept
      -> callable_result_t<
          _impl<is_tag_invocable_v<_fn, Operation&>>, Operation&> {
      return _impl<is_tag_invocable_v<_fn, Operation&>>{}(op);
    }
  } start{};

  template <>
  struct _fn::_impl<false> {
    template <typename Operation>
    auto operator()(Operation& op) const noexcept -> decltype(op.start()) {
      static_assert(
          noexcept(op.start()),
          "start() customisation must be noexcept");
      return op.start();
    }
  };
} // namespace _start
using _start::start;

namespace _execute2 {
  void execute();

  struct _fn {
    struct _impl; // defined below
    UNIFEX_TEMPLATE(typename E, typename F, typename Impl = _impl)
      (requires invocable<std::remove_cvref_t<F>&> &&
          constructible_from<std::remove_cvref_t<F>, F> &&
          move_constructible<std::remove_cvref_t<F>>) //&&
          //__valid<Impl, E, F>
    void operator()(E&& e, F&& f) const { //noexcept(__noexcept<Impl, E, F>) {
      (void) Impl{}((E&&) e, (F&&) f);
    }
  };
} // namespace _execute2

namespace _execute2_cpo {
  inline constexpr _execute2::_fn execute2 {};
}
using _execute2_cpo::execute2;

using invocable_archetype =
  struct _invocable_archetype {
    void operator()() & noexcept;
  };

template <typename E, typename F>
UNIFEX_CONCEPT_FRAGMENT( //
  _executor_of_impl_,    //
    requires(const E& e, F&& f) (
      unifex::execute2(e, (F&&) f)
    ));

template <typename E, typename F>
UNIFEX_CONCEPT        //
  _executor_of_impl = //
    invocable<std::remove_cvref_t<F>&> &&
    constructible_from<std::remove_cvref_t<F>, F> &&
    move_constructible<std::remove_cvref_t<F>> &&
    copy_constructible<E> &&
    equality_comparable<E> &&
    std::is_nothrow_copy_constructible_v<E> &&
    UNIFEX_FRAGMENT(unifex::_executor_of_impl_, E, F);

template <typename E>
UNIFEX_CONCEPT //
  executor =   //
    _executor_of_impl<E, invocable_archetype>;

template <typename E, typename F>
UNIFEX_CONCEPT  //
  executor_of = //
    executor<E> && _executor_of_impl<E, F>;

/// \cond
namespace detail {
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

  template <typename E>
  inline constexpr bool _is_executor<
    E,
    std::enable_if_t<_executor_of_impl<E, _as_invocable<_void_receiver, E>>>> = true;
} // namespace detail
/// \endcond

namespace _connect {
  template <typename E, typename R>
  inline constexpr bool _can_execute =
    _executor_of_impl<E, detail::_as_invocable<std::remove_cvref_t<R>, E>>;
  template <typename E, typename F>
  inline constexpr bool _can_execute<E, detail::_as_receiver<F, E>> =
    false;

  template <typename Sender, typename Receiver>
  using _member_connect_result_t =
      decltype((static_cast<Sender&&(*)()>(nullptr)()).connect(
          static_cast<Receiver&&(*)()>(nullptr)()));
  template <typename Sender, typename Receiver>
  UNIFEX_CONCEPT_FRAGMENT( //
    _has_member_connect_,  //
      requires() (         //
        typename(_member_connect_result_t<Sender, Receiver>)
      ));
  template <typename Sender, typename Receiver>
  UNIFEX_CONCEPT //
    _has_member_connect = //
      UNIFEX_FRAGMENT(_connect::_has_member_connect_, Sender, Receiver);

  inline constexpr struct _fn {
   private:
    template <typename Executor, typename Receiver>
    struct _as_op {
      struct type {
        std::remove_cvref_t<Executor> e_;
        std::remove_cvref_t<Receiver> r_;
        void start() noexcept try {
          using _as_invocable =
            detail::_as_invocable<std::remove_cvref_t<Receiver>, Executor>;
          unifex::execute2(std::move(e_), _as_invocable{r_});
        } catch(...) {
          unifex::set_error(std::move(r_), std::current_exception());
        }
      };
    };
    template <typename Executor, typename Receiver>
    using _as_operation = typename _as_op<Executor, Receiver>::type;

    struct _with_tag_invoke_fn {
      template <typename Sender, typename Receiver>
      auto operator()(Sender&& s, Receiver&& r) const
          noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Receiver>) ->
          tag_invoke_result_t<_fn, Sender, Receiver> {
        return unifex::tag_invoke(_fn{}, (Sender &&) s, (Receiver &&) r);
      }
    };
    struct _with_member_connect_fn {
      template <typename Sender, typename Receiver>
      auto operator()(Sender&& s, Receiver&& r) const
          noexcept(noexcept(((Sender &&) s).connect((Receiver &&) r))) ->
          _member_connect_result_t<Sender, Receiver> {
        return ((Sender &&) s).connect((Receiver &&) r);
      }
    };
    struct _with_execute_fn {
      template <typename Executor, typename Receiver>
      auto operator()(Executor&& e, Receiver&& r) const
        noexcept(std::is_nothrow_constructible_v<
            _as_operation<Executor, Receiver>, Executor, Receiver>) {
        return _as_operation<Executor, Receiver>{(Executor &&) e, (Receiver &&) r};
      }
    };
    struct _connect_failed_fn {
      UNIFEX_TEMPLATE(typename Sender, typename Receiver)
        // We have already tried all the alternative connect implementations,
        // and we didn't find any that worked. Put all the failed alternatives
        // into a requires clause and let the compiler report what failed and
        // (hopefully) why.
        (requires receiver<Receiver> &&
          ((sender<Sender> && is_tag_invocable_v<_fn, Sender, Receiver>) ||
           (sender<Sender> && _has_member_connect<Sender, Receiver>) ||
           (receiver_of<Receiver> && _can_execute<Sender, Receiver>)))
      auto operator()(Sender&&, Receiver&&) const noexcept {
        struct _op {
          void start() & noexcept;
        };
        return _op{};
      } 
    };
    // Select a connect implementation strategy by first looking for tag_invoke,
    // then for a .connect() member function, and finally by testing whether or
    // not we've been passed an executor and a nullary callable.
    template <typename Sender, typename Receiver>
    static auto _select_impl() noexcept {
      if constexpr ((bool)sender<Sender> && is_tag_invocable_v<_fn, Sender, Receiver>) {
        return _with_tag_invoke_fn{};
      } else if constexpr (sender<Sender> && _has_member_connect<Sender, Receiver>) {
        return _with_member_connect_fn{};
      } else if constexpr (receiver_of<Receiver> && _can_execute<Sender, Receiver>) {
        return _with_execute_fn{};
      } else {
        return _connect_failed_fn{};
      }
    }
    template <typename Sender, typename Receiver>
    using _impl = decltype(_select_impl<Sender, Receiver>());
   public:
    UNIFEX_TEMPLATE(typename Sender, typename Receiver)
      (requires receiver<Receiver>)
    auto operator()(Sender&& s, Receiver&& r) const
      noexcept(is_nothrow_callable_v<_impl<Sender, Receiver>, Sender, Receiver>) ->
      decltype(_impl<Sender, Receiver>{}((Sender &&) s, (Receiver &&) r)) {
      return _impl<Sender, Receiver>{}((Sender &&) s, (Receiver &&) r);
    }
  } connect{};
} // namespace _connect
using _connect::connect;

template<class Sender, class Receiver>
UNIFEX_CONCEPT_FRAGMENT( //
  _sender_to, //
    requires (Sender&& s, Receiver&& r) ( //
      connect((Sender&&) s, (Receiver&&) r)
    ));
template<class Sender, class Receiver>
UNIFEX_CONCEPT //
  sender_to =
    sender<Sender> &&
    receiver<Receiver> &&
    UNIFEX_FRAGMENT(_sender_to, Sender, Receiver);

namespace lazy {
  template<class Sender>
  UNIFEX_CONCEPT_DEFER //
    sender = //
      UNIFEX_DEFER(unifex::sender, Sender);

  template<class Sender, class Receiver>
  UNIFEX_CONCEPT_DEFER //
    sender_to =
      UNIFEX_DEFER(unifex::sender_to, Sender, Receiver);
} // namespace lazy

template<class Sender, class Receiver>
using connect_result_t =
  decltype(connect(
    static_cast<Sender(*)()>(nullptr)(),
    static_cast<Receiver(*)()>(nullptr)()));

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
