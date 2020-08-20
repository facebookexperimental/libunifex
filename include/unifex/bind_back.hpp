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

struct instance_of_apply_target {};

template <typename Cpo, typename... ArgN>
struct _apply_target {
  struct type;
};

template <typename Cpo, typename... ArgN>
using apply_target = typename _apply_target<Cpo, ArgN...>::type;

template <typename Cpo, typename... ArgN>
struct _apply_target<Cpo, ArgN...>::type : public instance_of_apply_target {
 private:
  std::tuple<remove_cvref_t<ArgN>...> argN_;
 public:
  template <typename... CN>
  explicit constexpr type(CN&&... cn) 
    noexcept(std::is_nothrow_constructible_v<std::tuple<remove_cvref_t<ArgN>...>, CN...>) 
    : argN_((CN&&) cn...) {}

  template (typename Target)
    (requires (!std::is_base_of_v<instance_of_apply_target, remove_cvref_t<Target>>))
  auto operator()(Target&& target) const &
    noexcept(
      is_nothrow_callable_v<Cpo, Target, ArgN...>) 
    -> callable_result_t<Cpo, Target, ArgN...> {
    return std::apply([&](auto&&... argN){
      return Cpo{}((Target&&) target, argN...);
    }, argN_);
  }
  template (typename Target)
    (requires (!std::is_base_of_v<instance_of_apply_target, remove_cvref_t<Target>>))
  auto operator()(Target&& target) &&
    noexcept(
      is_nothrow_callable_v<Cpo, Target, ArgN...>) 
    -> callable_result_t<Cpo, Target, ArgN...> {
    return std::apply([&](auto&&... argN){
      return Cpo{}((Target&&) target, (ArgN&&) argN...);
    }, std::move(argN_));
  }

  template (typename Target)
    (requires (!std::is_base_of_v<instance_of_apply_target, remove_cvref_t<Target>>))
  friend auto operator|(Target&& target, type&& self) 
    noexcept(
      is_nothrow_callable_v<Cpo, Target, ArgN...>) 
    -> callable_result_t<Cpo, Target, ArgN...> {
    return std::move(self)((Target&&) target);
  }
  template (typename Target)
    (requires (!std::is_base_of_v<instance_of_apply_target, remove_cvref_t<Target>>))
  friend auto operator|(Target&& target, const type& self) 
    noexcept(
      is_nothrow_callable_v<Cpo, Target, ArgN...>) 
    -> callable_result_t<Cpo, Target, ArgN...> {
    return self((Target&&) target);
  }

  struct compose {
    template (typename Target, typename Other, typename Self)
    (requires (!std::is_base_of_v<instance_of_apply_target, remove_cvref_t<Target>>) AND
        std::is_base_of_v<instance_of_apply_target, remove_cvref_t<Other>> AND
        std::is_same_v<remove_cvref_t<Self>, type>)
    auto operator()(Target&& target, Other&& other, Self&& self) const 
      noexcept(
        is_nothrow_callable_v<Other, Target> &&
        is_nothrow_callable_v<Self, callable_result_t<Other, Target>>
      ) 
      -> callable_result_t<Self, callable_result_t<Other, Target>> {
      return ((Self&&)self)(((Other&&)other)((Target&&)target));
    }
  };

  template (typename Other, typename Self)
    (requires std::is_base_of_v<instance_of_apply_target, remove_cvref_t<Other>> AND
      std::is_same_v<remove_cvref_t<Self>, type>)
  friend apply_target<compose, Other, Self> 
  operator|(Other&& other, Self&& self) 
    noexcept(
      std::is_nothrow_constructible_v<
        apply_target<compose, Other, Self>,
        Other,
        Self
      >) {
    return apply_target<compose, Other, Self>{
      (Other&&)other, (Self&&)self};
  }
};

inline const struct _fn {
public:

  template <typename Cpo, typename... ArgN>
  auto operator()(Cpo&& cpo, ArgN&&... argN) const 
      noexcept(
          std::is_nothrow_constructible_v<apply_target<remove_cvref_t<Cpo>, ArgN...>, ArgN...>) 
      -> apply_target<remove_cvref_t<Cpo>, ArgN...> {
    return apply_target<remove_cvref_t<Cpo>, ArgN...>{(ArgN &&) argN...};
  }
} bind_back{};

} // namespace _bind_back

using _bind_back::bind_back;

template <typename Cpo, typename... ArgN>
using bind_back_result_t = _bind_back::apply_target<remove_cvref_t<Cpo>, ArgN...>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
