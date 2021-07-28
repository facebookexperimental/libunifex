/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2 with LLVM exceptions
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

#include <unifex/detail/prologue.hpp>

namespace unifex {

struct unstoppable_token {
  template <typename F>
  struct callback_type {
    explicit callback_type(unstoppable_token, F&&) noexcept {}
  };
  static constexpr bool stop_requested() noexcept {
    return false;
  }
  static constexpr bool stop_possible() noexcept {
    return false;
  }
};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
