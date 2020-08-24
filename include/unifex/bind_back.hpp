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

#include <type_traits>
#include <tuple>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _bind_back {

namespace detail {

struct compose {
  template <typename Target, typename Other, typename Self>
  auto operator()(Target&& target, Other&& other, Self&& self) const 
    noexcept(
      is_nothrow_callable_v<Other, Target> &&
      is_nothrow_callable_v<Self, callable_result_t<Other, Target>>
    ) 
    -> callable_result_t<Self, callable_result_t<Other, Target>> {
    return ((Self&&)self)(((Other&&)other)((Target&&)target));
  }
};

} // namespace detail

template <typename Cpo, typename Target>
struct _apply_fn_impl {
  struct type;
};

template <typename Cpo, typename... ArgN>
using _apply_fn = typename _apply_fn_impl<Cpo, ArgN...>::type;

template <typename Cpo, typename Target>
struct _apply_fn_impl<Cpo, Target>::type {
  std::remove_reference_t<Target>* target_;

  template <typename... _ArgN>
  using _result_t =
    typename meta_quote1_<callable_result_t>::template apply<Cpo, Target, _ArgN...>;

  template (typename... ArgN)
    (requires callable<Cpo, Target, ArgN...>)
  auto operator()(ArgN&&... argN) 
    noexcept(
      is_nothrow_callable_v<Cpo, Target, ArgN...>) 
    -> _result_t<ArgN...> {
    return Cpo{}((Target&&) *target_, (ArgN&&)argN...);
  }
};

struct _result_base {};

template <typename Cpo, typename... ArgN>
struct _result_impl {
  struct type;
};

template <typename Cpo, typename... ArgN>
using _result = typename _result_impl<Cpo, ArgN...>::type;

template <typename Cpo, typename... ArgN>
struct _result_impl<Cpo, ArgN...>::type : _result_base {
 private:
  std::tuple<remove_cvref_t<ArgN>...> argN_;
 public:
  template <typename... CN>
  explicit constexpr type(CN&&... cn) 
    noexcept(std::is_nothrow_constructible_v<std::tuple<remove_cvref_t<ArgN>...>, CN...>) 
    : argN_((CN&&) cn...) {}

  template <typename Target, typename... _ArgN>
  using _result_t =
    typename meta_quote1_<callable_result_t>::template apply<Cpo, Target, _ArgN...>;

  template (typename Target)
    (requires (!derived_from<Target, _result_base>) AND callable<Cpo, Target, ArgN const&...>)
  auto operator()(Target&& target) const &
    noexcept(
      is_nothrow_callable_v<Cpo, Target, ArgN const&...>) 
    -> _result_t<Target, ArgN const&...> {
    return std::apply(_apply_fn<Cpo, Target>{&target}, argN_);
  }
  template (typename Target)
    (requires (!derived_from<Target, _result_base>) AND callable<Cpo, Target, remove_cvref_t<ArgN>...>)
  auto operator()(Target&& target) &&
    noexcept(
      is_nothrow_callable_v<Cpo, Target, remove_cvref_t<ArgN>...>) 
    -> _result_t<Target, remove_cvref_t<ArgN>...> {
    return std::apply(_apply_fn<Cpo, Target>{&target}, std::move(argN_));
  }

  template (typename Target)
    (requires (!derived_from<Target, _result_base>) AND callable<Cpo, Target, remove_cvref_t<ArgN>...>)
  friend auto operator|(Target&& target, type&& self) 
    noexcept(
      is_nothrow_callable_v<Cpo, Target, remove_cvref_t<ArgN>...>) 
    -> _result_t<Target, remove_cvref_t<ArgN>...> {
    return std::move(self)((Target&&) target);
  }
  template (typename Target)
    (requires (!derived_from<Target, _result_base>) AND callable<Cpo, Target, ArgN const&...>)
  friend auto operator|(Target&& target, const type& self) 
    noexcept(
      is_nothrow_callable_v<Cpo, Target, ArgN const&...>) 
    -> _result_t<Target, ArgN const&...> {
    return self((Target&&) target);
  }

  template (typename Other, typename Self)
    (requires derived_from<Other, _result_base> AND same_as<remove_cvref_t<Self>, type>)
  friend _result<detail::compose, Other, Self> 
  operator|(Other&& other, Self&& self) 
    noexcept(
      std::is_nothrow_constructible_v<
        _result<detail::compose, Other, Self>,
        Other,
        Self
      >) {
    return _result<detail::compose, Other, Self>{
      (Other&&)other, (Self&&)self};
  }
};

inline const struct _fn {
public:

  template <typename Cpo, typename... ArgN>
  auto operator()(Cpo&& cpo, ArgN&&... argN) const 
      noexcept(
          std::is_nothrow_constructible_v<_result<remove_cvref_t<Cpo>, ArgN...>, ArgN...>) 
      -> _result<remove_cvref_t<Cpo>, ArgN...> {
    return _result<remove_cvref_t<Cpo>, ArgN...>{(ArgN &&) argN...};
  }
} bind_back{};

} // namespace _bind_back

using _bind_back::bind_back;

template <typename Cpo, typename... ArgN>
using bind_back_result_t = _bind_back::_result<remove_cvref_t<Cpo>, ArgN...>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
