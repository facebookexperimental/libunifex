#pragma once

#include <thread>

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
