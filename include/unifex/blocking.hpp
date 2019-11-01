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

namespace cpo {

inline constexpr struct blocking_cpo {
  template <typename Sender>
  friend constexpr blocking_kind tag_invoke(
      blocking_cpo,
      const Sender& s) noexcept {
    return blocking_kind::maybe;
  }

  template <typename Sender>
  constexpr auto operator()(const Sender& s) const
      noexcept(noexcept(tag_invoke(*this, s)))
          -> decltype(tag_invoke(*this, s)) {
    return tag_invoke(*this, s);
  }
} blocking;

} // namespace cpo
} // namespace unifex
