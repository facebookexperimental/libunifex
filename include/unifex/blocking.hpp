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

#include <unifex/tag_invoke.hpp>

namespace unifex {

enum class blocking_kind {
  // No guarantees about the timing and context on which the receiver will
  // be called.
  maybe,

  // Always completes asynchronously.
  // Guarantees that the receiver will not be called on the current thread
  // before .start() returns. The receiver may be called on another thread
  // before .start() returns, however, or may be called on the current thread
  // some time after .start() returns.
  never,

  // Guarantees that the receiver will be called strongly-happens-before
  // .start() returns. Does not guarantee the call to the receiver happens
  // on the same thread that called .start(), however.
  always,

  // Caller guarantees that the receiver will be called inline on the
  // current thread that called .start() before .start() returns.
  always_inline
};

inline constexpr struct blocking_cpo {
  template <typename Sender>
  friend constexpr blocking_kind tag_invoke(
      blocking_cpo,
      const Sender&) noexcept {
    return blocking_kind::maybe;
  }

  template <typename Sender>
  constexpr auto operator()(const Sender& s) const
      noexcept(noexcept(tag_invoke(*this, s)))
          -> decltype(tag_invoke(*this, s)) {
    return tag_invoke(*this, s);
  }
} blocking;

} // namespace unifex
