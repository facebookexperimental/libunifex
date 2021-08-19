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

#include <thread>

#include <unifex/detail/prologue.hpp>

namespace unifex {

class spin_wait {
 public:
  spin_wait() noexcept = default;

  void wait() noexcept {
    if (count_++ < yield_threshold) {
      // TODO: _mm_pause();
    } else {
      if (count_ == 0) {
        count_ = yield_threshold;
      }
      std::this_thread::yield();
    }
  }

 private:
  static constexpr std::uint32_t yield_threshold = 20;

  std::uint32_t count_ = 0;
};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
