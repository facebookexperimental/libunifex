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

namespace _compose_with_target {

template <typename Cpo, typename... ArgN>
struct apply_target {
private:
  std::tuple<remove_cvref_t<ArgN>...> argN_;
public:
  template <typename... CN>
  explicit apply_target(CN&&... cn) : argN_((CN&&) cn...) {}

  template <typename Target>
  friend auto operator|(Target&& target, apply_target&& self) 
    noexcept(
      is_nothrow_callable_v<Cpo, Target, ArgN...>) 
    -> callable_result_t<Cpo, Target, ArgN...> {
    return std::apply([&](auto&&... argN){
      return Cpo{}((Target&&) target, (ArgN&&) argN...);
    }, std::move(self.argN_));
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
} compose_with_target{};

} // namespace _compose_with_target

using _compose_with_target::compose_with_target;

template <typename Cpo, typename... ArgN>
using compose_with_target_result_t = _compose_with_target::apply_target<remove_cvref_t<Cpo>, ArgN...>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
