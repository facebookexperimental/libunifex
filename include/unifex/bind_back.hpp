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

#include <type_traits>
#include <tuple>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _compose {

struct _fn {
  template <typename Target, typename Other, typename Self>
  auto operator()(Target&& target, Other&& other, Self&& self) const
    noexcept(
      is_nothrow_callable_v<Other, Target> &&
      is_nothrow_callable_v<Self, callable_result_t<Other, Target>>)
    -> callable_result_t<Self, callable_result_t<Other, Target>> {
    return ((Self&&) self)(((Other&&) other)((Target&&) target));
  }
};

} // namespace _compose

namespace _bind_back {

template <typename Cpo, typename Target>
struct _apply_fn_impl {
  struct type;
};

template <typename Cpo, typename... ArgN>
using _apply_fn = typename _apply_fn_impl<Cpo, ArgN...>::type;

template <typename Cpo, typename Target>
struct _apply_fn_impl<Cpo, Target>::type {
  Cpo&& cpo_;
  Target&& target_;

  template (typename... ArgN)
    (requires callable<Cpo, Target, ArgN...>)
  auto operator()(ArgN&&... argN)
    noexcept(is_nothrow_callable_v<Cpo, Target, ArgN...>)
    -> callable_result_t<Cpo, Target, ArgN...> {
    return ((Cpo&&) cpo_)((Target&&) target_, (ArgN&&) argN...);
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
  UNIFEX_NO_UNIQUE_ADDRESS Cpo cpo_;
  std::tuple<ArgN...> argN_;

  template (typename Target)
    (requires (!derived_from<remove_cvref_t<Target>, _result_base>) AND
      callable<Cpo const&, Target, ArgN const&...>)
  decltype(auto) operator()(Target&& target) const &
    noexcept(is_nothrow_callable_v<Cpo const&, Target, ArgN const&...>) {
    return std::apply(_apply_fn<Cpo const&, Target>{cpo_, (Target&&) target}, argN_);
  }
  template (typename Target)
    (requires (!derived_from<remove_cvref_t<Target>, _result_base>) AND
      callable<Cpo, Target, ArgN...>)
  decltype(auto) operator()(Target&& target) &&
    noexcept(is_nothrow_callable_v<Cpo, Target, ArgN...>) {
    return std::apply(
        _apply_fn<Cpo, Target>{(Cpo&&) cpo_, (Target&&) target},
        std::move(argN_));
  }

  template (typename Target, typename Self)
    (requires (!derived_from<remove_cvref_t<Target>, _result_base>) AND
      same_as<remove_cvref_t<Self>, type> AND
      callable<member_t<Self, Cpo>, Target, member_t<Self, ArgN>...>)
  friend decltype(auto) operator|(Target&& target, Self&& self)
    noexcept(
      is_nothrow_callable_v<member_t<Self, Cpo>, Target, member_t<Self, ArgN>...>) {
    return std::apply(
        _apply_fn<member_t<Self, Cpo>, Target>{((Self&&) self).cpo_, (Target&&) target},
        ((Self&&) self).argN_);
  }

  template (typename Other, typename Self)
    (requires derived_from<remove_cvref_t<Other>, _result_base> AND
      same_as<remove_cvref_t<Self>, type>)
  friend decltype(auto) operator|(Other&& other, Self&& self)
    noexcept(noexcept(
      _result<_compose::_fn, remove_cvref_t<Other>, type>{
          {}, // _result_base
          {}, // _compose::_fn
          {(Other&&) other, (Self&&) self}})) {
    return _result<_compose::_fn, remove_cvref_t<Other>, type>{
        {}, // _result_base
        {}, // _compose::_fn
        {(Other&&) other, (Self&&) self}};
  }
};

inline const struct _fn {
  template <typename Cpo, typename... ArgN>
  constexpr auto operator()(Cpo cpo, ArgN&&... argN) const
      noexcept(noexcept(
          _result<Cpo, std::decay_t<ArgN>...>{{}, (Cpo&&) cpo, {(ArgN &&) argN...}}))
      -> _result<Cpo, std::decay_t<ArgN>...> {
    return _result<Cpo, std::decay_t<ArgN>...>{{}, (Cpo&&) cpo, {(ArgN &&) argN...}};
  }
} bind_back{};

} // namespace _bind_back

using _bind_back::bind_back;

template <typename Cpo, typename... ArgN>
using bind_back_result_t =
    _bind_back::_result<std::decay_t<Cpo>, std::decay_t<ArgN>...>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
