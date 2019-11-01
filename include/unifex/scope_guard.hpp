#pragma once

#include <type_traits>
#include <functional>

namespace unifex {

template <typename Func>
struct scope_guard {
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

  scope_guard(Func&& func) noexcept(
      std::is_nothrow_move_constructible_v<Func>)
      : func_((Func &&) func) {}

  ~scope_guard() {
    static_assert(noexcept(std::invoke((Func &&) func_)));
    std::invoke((Func &&) func_);
  }
};

template <typename Func>
scope_guard(Func&& func)->scope_guard<Func>;

} // namespace unifex
