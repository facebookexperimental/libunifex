#pragma once

#include <exception>

namespace unifex {

struct null_receiver {
  void value() noexcept {}
  [[noreturn]] void done() noexcept {
    std::terminate();
  }
  template <typename Error>
  [[noreturn]] void error(Error&&) noexcept {
    std::terminate();
  }
};

} // namespace unifex
