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

#include <unifex/config.hpp>
#include <unifex/detail/unifex_fwd.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/std_concepts.hpp>

#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {
/// \cond
namespace detail {
  template <typename R, typename E>
  struct _as_invocable;

  template <typename F, typename S>
  struct _as_receiver {
    F f_;
    void set_value() noexcept(is_nothrow_callable_v<F&>) {
      f_();
    }
    [[noreturn]] void set_error(std::exception_ptr) noexcept {
      std::terminate();
    }
    void set_done() noexcept {}
  };

  struct _void_receiver {
    void set_value() noexcept;
    void set_error(std::exception_ptr) noexcept;
    void set_done() noexcept;
  };
} // namespace detail
/// \cond

namespace _execute_cpo {
  using detail::_can_submit;

  template <typename Executor, typename Fn>
  using _member_execute_result_t =
      decltype(UNIFEX_DECLVAL(Executor).execute(UNIFEX_DECLVAL(Fn)));
  template <typename Executor, typename Fn>
  UNIFEX_CONCEPT_FRAGMENT( //
    _has_member_execute_,  //
      requires() (         //
        typename(_member_execute_result_t<Executor, Fn>)
      ));
  template <typename Executor, typename Fn>
  UNIFEX_CONCEPT //
    _has_member_execute = //
      UNIFEX_FRAGMENT(_execute_cpo::_has_member_execute_, Executor, Fn);

  template <typename Fn>
  UNIFEX_CONCEPT //
    _lvalue_callable = //
      callable<remove_cvref_t<Fn>&> &&
      constructible_from<remove_cvref_t<Fn>, Fn> &&
      move_constructible<remove_cvref_t<Fn>>;

  inline const struct _fn {
    template(typename Executor, typename Fn)
      (requires _lvalue_callable<Fn> AND
          tag_invocable<_fn, Executor, Fn>)
    void operator()(Executor&& e, Fn&& fn) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Executor, Fn>) {
      unifex::tag_invoke(_fn{}, (Executor &&) e, (Fn &&) fn);
    }
    template(typename Executor, typename Fn)
      (requires _lvalue_callable<Fn> AND
          (!tag_invocable<_fn, Executor, Fn>) AND
          _has_member_execute<Executor, Fn>)
    void operator()(Executor&& e, Fn&& fn) const
        noexcept(noexcept(((Executor &&) e).execute((Fn &&) fn))) {
      ((Executor &&) e).execute((Fn &&) fn);
    }
    template(typename Sender, typename Fn)
      (requires _lvalue_callable<Fn> AND
          (!tag_invocable<_fn, Sender, Fn>) AND
          (!_has_member_execute<Sender, Fn>) AND
          _can_submit<Sender, Fn>)
    void operator()(Sender&& s, Fn&& fn) const {
      using _as_receiver =
          detail::_as_receiver<remove_cvref_t<Fn>, Sender>;
      unifex::submit((Sender&&) s, _as_receiver{(Fn&&) fn});
    }
  } execute {};
} // namespace _execute_cpo

using _execute_cpo::execute;

using invocable_archetype =
  struct _invocable_archetype {
    void operator()() & noexcept;
  };

#if UNIFEX_CXX_CONCEPTS
// Define the _executor_of_impl concept without macros for
// improved diagnostics.
template <typename E, typename F>
concept //
  _executor_of_impl = //
    invocable<remove_cvref_t<F>&> &&
    constructible_from<remove_cvref_t<F>, F> &&
    move_constructible<remove_cvref_t<F>> &&
    copy_constructible<E> &&
    equality_comparable<E> &&
    std::is_nothrow_copy_constructible_v<E> &&
    requires(const E e, F&& f) {
      unifex::execute(e, (F&&) f);
    };
#else
template <typename E, typename F>
UNIFEX_CONCEPT_FRAGMENT( //
  _executor_of_impl_part,    //
    requires(const E e, F&& f) (
      unifex::execute(e, (F&&) f)
    ));

template <typename E, typename F>
UNIFEX_CONCEPT        //
  _executor_of_impl = //
    invocable<remove_cvref_t<F>&> &&
    constructible_from<remove_cvref_t<F>, F> &&
    move_constructible<remove_cvref_t<F>> &&
    copy_constructible<E> &&
    equality_comparable<E> &&
    std::is_nothrow_copy_constructible_v<E> &&
    UNIFEX_FRAGMENT(unifex::_executor_of_impl_part, E, F);
#endif

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
  template <typename, typename>
  inline constexpr bool _is_executor = false;

  template <typename E>
  inline constexpr bool _is_executor<
    E,
    std::enable_if_t<_executor_of_impl<E, _as_invocable<_void_receiver, E>>>> = true;

  template <typename E, typename R, typename>
  inline constexpr bool _can_execute =
    _executor_of_impl<E, _as_invocable<remove_cvref_t<R>, E>>;

#if UNIFEX_CXX_CONCEPTS
  template <typename E1, typename F, typename E2>
    requires same_as<remove_cvref_t<E1>, remove_cvref_t<E2>>
  inline constexpr bool _can_execute<E1, _as_receiver<F, E2>> = false;
#else
  template <typename E1, typename F, typename E2>
  inline constexpr bool _can_execute<
      E1,
      _as_receiver<F, E2>,
      std::enable_if_t<UNIFEX_IS_SAME(remove_cvref_t<E1>, remove_cvref_t<E2>)>> =
    false;
#endif
} // namespace detail
/// \endcond
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/submit.hpp>
