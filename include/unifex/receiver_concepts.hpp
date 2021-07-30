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
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/detail/unifex_fwd.hpp>

#include <exception>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _rec_cpo {
  inline const struct _set_value_fn {
  private:
    template <typename Receiver, typename... Values>
    using set_value_member_result_t =
      decltype(UNIFEX_DECLVAL(Receiver).set_value(UNIFEX_DECLVAL(Values)...));
    template <typename Receiver, typename... Values>
    using _result_t =
      typename conditional_t<
        tag_invocable<_set_value_fn, Receiver, Values...>,
        meta_tag_invoke_result<_set_value_fn>,
        meta_quote1_<set_value_member_result_t>>::template apply<Receiver, Values...>;
  public:
    template(typename Receiver, typename... Values)
      (requires tag_invocable<_set_value_fn, Receiver, Values...>)
    auto operator()(Receiver&& r, Values&&... values) const
        noexcept(
            is_nothrow_tag_invocable_v<_set_value_fn, Receiver, Values...>)
        -> _result_t<Receiver, Values...> {
      return unifex::tag_invoke(
          _set_value_fn{}, (Receiver &&) r, (Values &&) values...);
    }
    template(typename Receiver, typename... Values)
      (requires (!tag_invocable<_set_value_fn, Receiver, Values...>))
    auto operator()(Receiver&& r, Values&&... values) const
        noexcept(noexcept(
            static_cast<Receiver&&>(r).set_value((Values &&) values...)))
        -> _result_t<Receiver, Values...> {
      return static_cast<Receiver&&>(r).set_value((Values &&) values...);
    }
  } set_value{};

  inline const struct _set_next_fn {
  private:
    template <typename Receiver, typename... Values>
    using set_next_member_result_t =
      decltype(UNIFEX_DECLVAL(Receiver&).set_next(UNIFEX_DECLVAL(Values)...));
    template <typename Receiver, typename... Values>
    using _result_t =
      typename conditional_t<
        tag_invocable<_set_next_fn, Receiver&, Values...>,
        meta_tag_invoke_result<_set_next_fn>,
        meta_quote1_<set_next_member_result_t>>::template apply<Receiver, Values...>;
  public:
    template(typename Receiver, typename... Values)
      (requires tag_invocable<_set_next_fn, Receiver, Values...>)
    auto operator()(Receiver& r, Values&&... values) const
        noexcept(
            is_nothrow_tag_invocable_v<_set_next_fn, Receiver, Values...>)
        -> _result_t<Receiver, Values...> {
      return unifex::tag_invoke(
          _set_next_fn{}, r, (Values &&) values...);
    }
    template(typename Receiver, typename... Values)
      (requires (!tag_invocable<_set_next_fn, Receiver&, Values...>))
    auto operator()(Receiver& r, Values&&... values) const
        noexcept(noexcept(
            r.set_next((Values &&) values...)))
        -> _result_t<Receiver, Values...> {
      return r.set_next((Values &&) values...);
    }
  } set_next{};

  inline const struct _set_error_fn {
  private:
    template <typename Receiver, typename Error>
    using set_error_member_result_t =
      decltype(UNIFEX_DECLVAL(Receiver).set_error(UNIFEX_DECLVAL(Error)));
    template <typename Receiver, typename Error>
    using _result_t =
      typename conditional_t<
        tag_invocable<_set_error_fn, Receiver, Error>,
        meta_tag_invoke_result<_set_error_fn>,
        meta_quote2<set_error_member_result_t>>::template apply<Receiver, Error>;
  public:
    template(typename Receiver, typename Error)
      (requires tag_invocable<_set_error_fn, Receiver, Error>)
    auto operator()(Receiver&& r, Error&& error) const noexcept
        -> _result_t<Receiver, Error> {
      static_assert(
          is_nothrow_tag_invocable_v<_set_error_fn, Receiver, Error>,
          "set_error() invocation is required to be noexcept.");
      static_assert(
        std::is_void_v<tag_invoke_result_t<_set_error_fn, Receiver, Error>>
      );
      return unifex::tag_invoke(
          _set_error_fn{}, (Receiver &&) r, (Error&&) error);
    }
    template(typename Receiver, typename Error)
      (requires (!tag_invocable<_set_error_fn, Receiver, Error>))
    auto operator()(Receiver&& r, Error&& error) const noexcept
        -> _result_t<Receiver, Error> {
      static_assert(
          noexcept(static_cast<Receiver&&>(r).set_error((Error &&) error)),
          "receiver.set_error() method must be nothrow invocable");
      return static_cast<Receiver&&>(r).set_error((Error&&) error);
    }
  } set_error{};

  inline const struct _set_done_fn {
  private:
    template <typename Receiver>
    using set_done_member_result_t =
      decltype(UNIFEX_DECLVAL(Receiver).set_done());
    template <typename Receiver>
    using _result_t =
      typename conditional_t<
        tag_invocable<_set_done_fn, Receiver>,
        meta_tag_invoke_result<_set_done_fn>,
        meta_quote1<set_done_member_result_t>>::template apply<Receiver>;
  public:
    template(typename Receiver)
      (requires tag_invocable<_set_done_fn, Receiver>)
    auto operator()(Receiver&& r) const noexcept
        -> _result_t<Receiver> {
      static_assert(
          is_nothrow_tag_invocable_v<_set_done_fn, Receiver>,
          "set_done() invocation is required to be noexcept.");
      static_assert(
        std::is_void_v<tag_invoke_result_t<_set_done_fn, Receiver>>
      );
      return unifex::tag_invoke(_set_done_fn{}, (Receiver &&) r);
    }
    template(typename Receiver)
      (requires (!tag_invocable<_set_done_fn, Receiver>))
    auto operator()(Receiver&& r) const noexcept
        -> _result_t<Receiver> {
      static_assert(
          noexcept(static_cast<Receiver&&>(r).set_done()),
          "receiver.set_done() method must be nothrow invocable");
      return static_cast<Receiver&&>(r).set_done();
    }
  } set_done{};
} // namespace _rec_cpo

using _rec_cpo::set_value;
using _rec_cpo::set_next;
using _rec_cpo::set_error;
using _rec_cpo::set_done;

template <typename T>
inline constexpr bool is_receiver_cpo_v = is_one_of_v<
    remove_cvref_t<T>,
    _rec_cpo::_set_value_fn,
    _rec_cpo::_set_next_fn,
    _rec_cpo::_set_error_fn,
    _rec_cpo::_set_done_fn>;


// HACK: Approximation for CPOs that should be forwarded through receivers
// as query operations.
template <typename T>
inline constexpr bool is_receiver_query_cpo_v = !is_one_of_v<
    remove_cvref_t<T>,
    _rec_cpo::_set_value_fn,
    _rec_cpo::_set_next_fn,
    _rec_cpo::_set_error_fn,
    _rec_cpo::_set_done_fn,
    _connect::_cpo::_fn>;

template <typename T>
using is_receiver_cpo = std::bool_constant<is_receiver_cpo_v<T>>;

#if UNIFEX_CXX_CONCEPTS
// Defined the receiver concepts without the macros for improved diagnostics
template <typename R, typename E = std::exception_ptr>
concept //
  receiver = //
    move_constructible<remove_cvref_t<R>> &&
    constructible_from<remove_cvref_t<R>, R> &&
    requires(remove_cvref_t<R>&& r, E&& e)
    {
      { set_done(std::move(r)) } noexcept;
      { set_error(std::move(r), (E&&) e) } noexcept;
    };

template <typename R, typename... An>
concept //
  receiver_of = //
    receiver<R> &&
    requires(remove_cvref_t<R>&& r, An&&... an) //
    {
      set_value(std::move(r), (An&&) an...);
    };
#else
template <typename R, typename E>
UNIFEX_CONCEPT_FRAGMENT(
  _receiver,
    requires(remove_cvref_t<R>&& r, E&& e) //
    (
      noexcept(set_done(std::move(r))),
      noexcept(set_error(std::move(r), (E&&) e))
    ));

template <typename R, typename E = std::exception_ptr>
UNIFEX_CONCEPT //
  receiver = //
    move_constructible<remove_cvref_t<R>> &&
    constructible_from<remove_cvref_t<R>, R> &&
    UNIFEX_FRAGMENT(unifex::_receiver, R, E);

template <typename T, typename... An>
UNIFEX_CONCEPT_FRAGMENT( //
  _receiver_of, //
    requires(remove_cvref_t<T>&& t, An&&... an) //
    (
      set_value(std::move(t), (An&&) an...)
    ));

template <typename R, typename... An>
UNIFEX_CONCEPT //
  receiver_of = //
    receiver<R> &&
    UNIFEX_FRAGMENT(unifex::_receiver_of, R, An...);
#endif

template <typename R, typename... An>
  inline constexpr bool is_nothrow_receiver_of_v =
    receiver_of<R, An...> &&
    is_nothrow_callable_v<decltype(set_value), R, An...>;

//////////////////
// Metafunctions for checking callability of specific receiver methods

template <typename R, typename... An>
inline constexpr bool is_next_receiver_v =
    is_callable_v<decltype(set_next), R&, An...>;

template <typename R, typename... An>
inline constexpr bool is_nothrow_next_receiver_v =
    is_nothrow_callable_v<decltype(set_next), R&, An...>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
