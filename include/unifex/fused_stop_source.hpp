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

#include <optional>

#include <unifex/inplace_stop_token.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _fss {

struct stop_callback {
  void operator()() noexcept { source_.request_stop(); }

  unifex::inplace_stop_source& source_;
};

template <typename StopToken>
using stop_callback_t =
    typename StopToken::template callback_type<stop_callback>;

template <typename... StopCallback>
struct fused_stop_callback {
  explicit fused_stop_callback(unifex::inplace_stop_source&) noexcept {}
};

template <typename First, typename... Rest>
struct fused_stop_callback<First, Rest...> {
  template <typename StopToken, typename... StopTokens>
  explicit fused_stop_callback(
      unifex::inplace_stop_source& source,
      StopToken first,
      StopTokens... rest)  //
      noexcept(std::is_nothrow_constructible_v<
               fused_stop_callback<Rest...>,
               decltype(source),
               decltype(rest)...>&&  //
                   std::is_nothrow_constructible_v<
                       First,
                       decltype(first),
                       stop_callback>)
    : callback_(std::move(first), stop_callback{source})
    , rest_(source, std::move(rest)...) {}

  UNIFEX_NO_UNIQUE_ADDRESS First callback_;
  UNIFEX_NO_UNIQUE_ADDRESS fused_stop_callback<Rest...> rest_;
};

template <typename... StopTokens>
struct fused_stop_source : unifex::inplace_stop_source {
  using fused_callback_type =
      fused_stop_callback<stop_callback_t<StopTokens>...>;

  void register_callbacks(StopTokens... tokens) {
    callbacks_.emplace(*this, std::move(tokens)...);
  }

  void deregister_callbacks() noexcept { callbacks_.reset(); }

private:
  UNIFEX_NO_UNIQUE_ADDRESS std::optional<fused_callback_type> callbacks_;
};
}  // namespace _fss

using _fss::fused_stop_source;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
