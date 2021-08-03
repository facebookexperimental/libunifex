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

#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _null {
  struct receiver {
    void set_value() noexcept {}
    [[noreturn]] void set_done() noexcept {
      std::terminate();
    }
    template <typename Error>
    [[noreturn]] void set_error(Error&&) noexcept {
      std::terminate();
    }
  };
} // namespace _null
using null_receiver = _null::receiver;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
