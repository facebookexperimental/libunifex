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

#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {

template <typename Func>
struct scope_guard {
private:
  static_assert(std::is_nothrow_move_constructible_v<Func>);
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  bool released_ = false;

public:
  scope_guard(Func&& func) noexcept
      : func_((Func &&) func) {}

  ~scope_guard() {
    reset();
  }

  void release() noexcept {
    released_ = true;
  }

  void reset() noexcept {
    static_assert(noexcept(((Func &&) func_)()));
    if (!std::exchange(released_, true)) {
      ((Func &&) func_)();
    }
  }
};

template <typename Func>
scope_guard(Func&& func) -> scope_guard<Func>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
