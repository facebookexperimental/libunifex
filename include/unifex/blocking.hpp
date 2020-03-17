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
#include <unifex/type_traits.hpp>

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

namespace _blocking {
inline constexpr struct _fn {
 private:
  template <bool>
  struct _impl {
    template <typename Sender>
    constexpr blocking_kind operator()(const Sender&) noexcept {
      return blocking_kind::maybe;
    }
  };
 public:
  template <typename Sender>
  constexpr auto operator()(const Sender& s) const
      noexcept(is_nothrow_callable_v<
          _impl<is_tag_invocable_v<_fn, const Sender&>>, const Sender&>)
      -> callable_result_t<
          _impl<is_tag_invocable_v<_fn, const Sender&>>, const Sender&> {
    return _impl<is_tag_invocable_v<_fn, const Sender&>>{}(s);
  }
} blocking{};

template <>
struct _fn::_impl<true> {
  template <typename Sender>
  constexpr auto operator()(const Sender& s)
      noexcept(is_nothrow_tag_invocable_v<_fn, const Sender&>)
      -> tag_invoke_result_t<_fn, const Sender&> {
    return tag_invoke(_fn{}, s);
  }
};
} // namespace _blocking
using _blocking::blocking;

} // namespace unifex
