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

#include <unifex/stop_token_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/unstoppable_token.hpp>

namespace unifex {

inline constexpr struct get_stop_token_cpo {
  template <typename T>
  auto operator()([[maybe_unused]] const T& value) const noexcept ->
      typename std::conditional_t<
          is_tag_invocable_v<get_stop_token_cpo, const T&>,
          tag_invoke_result<get_stop_token_cpo, const T&>,
          identity<unstoppable_token>>::type {
    if constexpr (is_tag_invocable_v<get_stop_token_cpo, const T&>) {
      static_assert(
          is_nothrow_tag_invocable_v<get_stop_token_cpo, const T&>,
          "get_stop_token() customisations must be declared noexcept");
      return tag_invoke(get_stop_token_cpo{}, value);
    } else {
      return unstoppable_token{};
    }
  }
} get_stop_token{};

template <typename T>
using get_stop_token_result_t =
    std::invoke_result_t<decltype(get_stop_token), T>;

template <typename Receiver>
using stop_token_type_t =
    std::remove_cvref_t<get_stop_token_result_t<Receiver>>;

} // namespace unifex
