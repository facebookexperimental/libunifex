#pragma once

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
