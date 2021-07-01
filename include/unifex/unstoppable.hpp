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

#include <unifex/unstoppable_token.hpp>
#include <unifex/with_query_value.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _unstoppable {
inline const struct _fn {
  template <typename Sender>
  constexpr auto operator()(Sender&& sender) const noexcept {
    return with_query_value(
        (Sender &&) sender, get_stop_token, unstoppable_token{});
  }
} unstoppable{};
}  // namespace _unstoppable

using _unstoppable::unstoppable;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
