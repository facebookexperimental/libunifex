/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _block {
enum class _enum {
  // No guarantees about the timing and context on which the receiver will
  // be called.
  maybe = 0,

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

struct blocking_kind {
  blocking_kind() = default;

  constexpr blocking_kind(_enum kind) noexcept
    : value(kind)
  {}

  template <_enum Kind>
  constexpr blocking_kind(std::integral_constant<_enum, Kind>) noexcept
    : value(Kind)
  {}

  constexpr operator _enum() const noexcept {
    return value;
  }

  constexpr _enum operator()() const noexcept {
    return value;
  }

  friend constexpr bool operator==(blocking_kind a, blocking_kind b) noexcept {
    return a.value == b.value;
  }

  friend constexpr bool operator!=(blocking_kind a, blocking_kind b) noexcept {
    return a.value != b.value;
  }

  static constexpr struct _maybe
    : std::integral_constant<_enum, _enum::maybe>
  {} const maybe {};

  static constexpr struct _never
    : std::integral_constant<_enum, _enum::never>
  {} const never {};

  static constexpr struct _always
    : std::integral_constant<_enum, _enum::always>
  {} const always {};

  static constexpr struct _always_inline
    : std::integral_constant<_enum, _enum::always_inline>
  {} const always_inline {};

  _enum value;
};

struct _fn {
  template(typename Sender)
    (requires tag_invocable<_fn, const Sender&>)
  constexpr auto operator()(const Sender& s) const
      noexcept(is_nothrow_tag_invocable_v<_fn, const Sender&>)
      -> tag_invoke_result_t<_fn, const Sender&> {
    return tag_invoke(_fn{}, s);
  }
  template(typename Sender)
    (requires (!tag_invocable<_fn, const Sender&>))
  constexpr auto operator()(const Sender&) const noexcept {
    return blocking_kind::maybe;
  }
};

namespace _cfn {
  template <_enum Kind>
  static constexpr _enum _kind(std::integral_constant<_enum, Kind>) noexcept {
    return Kind;
  }
  static constexpr _enum _kind(blocking_kind) noexcept {
    return _enum{0};
  }

  template <typename T>
  constexpr _enum cblocking() noexcept {
    using blocking_t = remove_cvref_t<decltype(_fn{}(UNIFEX_DECLVAL(T&)))>;
    return _kind(blocking_t{});
  }
}

} // namespace _block

inline constexpr _block::_fn blocking {};
using _block::_cfn::cblocking;
using _block::blocking_kind;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
