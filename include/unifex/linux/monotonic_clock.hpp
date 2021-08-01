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

#include <chrono>
#include <cstdint>
#include <limits>
#include <ratio>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace linuxos {

// A std::chrono-like clock type that wraps the Linux CLOCK_MONOTONIC clock
// type.
//
// This is the clock-type used for timers by the io_uring IORING_OP_TIMEOUT
// operations with absolute times.
class monotonic_clock {
 public:
  using rep = std::int64_t;
  using ratio = std::ratio<1, 10'000'000>; // 100ns
  using duration = std::chrono::duration<rep, ratio>;

  static constexpr bool is_steady = true;

  class time_point {
   public:
    using duration = monotonic_clock::duration;

    constexpr time_point() noexcept : seconds_(0), nanoseconds_(0) {}

    constexpr time_point(const time_point&) noexcept = default;

    time_point& operator=(const time_point&) noexcept = default;

    static constexpr time_point max() noexcept {
      time_point tp;
      tp.seconds_ = std::numeric_limits<std::int64_t>::max();
      tp.nanoseconds_ = 999'999'999;
      return tp;
    }

    static constexpr time_point min() noexcept {
      time_point tp;
      tp.seconds_ = std::numeric_limits<std::int64_t>::min();
      tp.nanoseconds_ = -999'999'999;
      return tp;
    }

    static time_point from_seconds_and_nanoseconds(
        std::int64_t seconds,
        long long nanoseconds) noexcept;

    constexpr std::int64_t seconds_part() const noexcept {
      return seconds_;
    }

    constexpr long long nanoseconds_part() const noexcept {
      return nanoseconds_;
    }

    template <typename Rep, typename Ratio>
    time_point& operator+=(
        const std::chrono::duration<Rep, Ratio>& d) noexcept;

    template <typename Rep, typename Ratio>
    time_point& operator-=(
        const std::chrono::duration<Rep, Ratio>& d) noexcept;

    friend duration operator-(
        const time_point& a,
        const time_point& b) noexcept {
      return duration(
          (a.seconds_ - b.seconds_) * 10'000'000 +
          (a.nanoseconds_ - b.nanoseconds_) / 100);
    }

    template <typename Rep, typename Ratio>
    friend time_point operator+(
        const time_point& a,
        std::chrono::duration<Rep, Ratio> d) noexcept {
      time_point tp = a;
      tp += d;
      return tp;
    }

    template <typename Rep, typename Ratio>
    friend time_point operator-(
        const time_point& a,
        std::chrono::duration<Rep, Ratio> d) noexcept {
      time_point tp = a;
      tp -= d;
      return tp;
    }

    friend bool operator==(const time_point& a, const time_point& b) noexcept {
      return a.seconds_ == b.seconds_ && a.nanoseconds_ == b.nanoseconds_;
    }

    friend bool operator!=(const time_point& a, const time_point& b) noexcept {
      return !(a == b);
    }

    friend bool operator<(const time_point& a, const time_point& b) noexcept {
      return (a.seconds_ == b.seconds_) ? (a.nanoseconds_ < b.nanoseconds_)
                                        : (a.seconds_ < b.seconds_);
    }

    friend bool operator>(const time_point& a, const time_point& b) noexcept {
      return b < a;
    }

    friend bool operator<=(const time_point& a, const time_point& b) noexcept {
      return !(b < a);
    }

    friend bool operator>=(const time_point& a, const time_point& b) noexcept {
      return !(a < b);
    }

   private:
    void normalize() noexcept;

    std::int64_t seconds_;
    long long nanoseconds_;
  };

  static time_point now() noexcept;
};

inline monotonic_clock::time_point
monotonic_clock::time_point::from_seconds_and_nanoseconds(
    std::int64_t seconds,
    long long nanoseconds) noexcept {
  time_point tp;
  tp.seconds_ = seconds;
  tp.nanoseconds_ = nanoseconds;
  tp.normalize();
  return tp;
}

template <typename Rep, typename Ratio>
monotonic_clock::time_point& monotonic_clock::time_point::operator+=(
    const std::chrono::duration<Rep, Ratio>& d) noexcept {
  const auto wholeSeconds = std::chrono::duration_cast<std::chrono::seconds>(d);
  const auto remainderNanoseconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(d - wholeSeconds);
  seconds_ += wholeSeconds.count();
  nanoseconds_ += remainderNanoseconds.count();
  normalize();
  return *this;
}

template <typename Rep, typename Ratio>
monotonic_clock::time_point& monotonic_clock::time_point::operator-=(
    const std::chrono::duration<Rep, Ratio>& d) noexcept {
  const auto wholeSeconds = std::chrono::duration_cast<std::chrono::seconds>(d);
  const auto remainderNanoseconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(d - wholeSeconds);
  seconds_ -= wholeSeconds.count();
  nanoseconds_ -= remainderNanoseconds.count();
  normalize();
  return *this;
}

inline void monotonic_clock::time_point::normalize() noexcept {
  constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
  auto extraSeconds = nanoseconds_ / nanoseconds_per_second;
  seconds_ += extraSeconds;
  nanoseconds_ -= extraSeconds * nanoseconds_per_second;
  if (seconds_ < 0 && nanoseconds_ > 0) {
    seconds_ += 1;
    nanoseconds_ -= nanoseconds_per_second;
  } else if (seconds_ > 0 && nanoseconds_ < 0) {
    seconds_ -= 1;
    nanoseconds_ += nanoseconds_per_second;
  }
}

} // namespace linuxos
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
