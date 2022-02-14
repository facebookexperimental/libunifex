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

#include <unifex/detail/prologue.hpp>

namespace unifex::win32 {

class filetime_clock {
public:
    using rep = std::int64_t;
    using ratio = std::ratio<1, 10'000'000>; // 100ns
    using duration = std::chrono::duration<rep, ratio>;

    static constexpr bool is_steady = false;

    class time_point {
    public:
        using duration = filetime_clock::duration;

        constexpr time_point() noexcept : ticks_(0) {}

        constexpr time_point(const time_point&) noexcept = default;

        time_point& operator=(const time_point&) noexcept = default;

        std::uint64_t get_ticks() const noexcept {
            return ticks_;
        }

        static constexpr time_point max() noexcept {
            time_point tp;
            tp.ticks_ = std::numeric_limits<std::int64_t>::max();
            return tp;
        }

        static constexpr time_point min() noexcept {
            return time_point{};
        }

        static time_point from_ticks(std::uint64_t ticks) noexcept {
            time_point tp;
            tp.ticks_ = ticks;
            return tp;
        }

        template <typename Rep, typename Ratio>
        time_point& operator+=(
            const std::chrono::duration<Rep, Ratio>& d) noexcept {
            ticks_ += std::chrono::duration_cast<duration>(d).count();
            return *this;
        }

        template <typename Rep, typename Ratio>
        time_point& operator-=(
            const std::chrono::duration<Rep, Ratio>& d) noexcept {
            ticks_ -= std::chrono::duration_cast<duration>(d).count();
            return *this;
        }

        friend duration operator-(time_point a, time_point b) noexcept {
            return duration{a.ticks_} - duration{b.ticks_};
        }

        template <typename Rep, typename Ratio>
        friend time_point operator+(time_point t, std::chrono::duration<Rep, Ratio> d) noexcept {
            time_point tp = t;
            tp += d;
            return tp;
        }

        friend bool operator==(time_point a, time_point b) noexcept {
            return a.ticks_ == b.ticks_;
        }

        friend bool operator!=(time_point a, time_point b) noexcept {
            return a.ticks_ != b.ticks_;
        }

        friend bool operator<(time_point a, time_point b) noexcept {
            return a.ticks_ < b.ticks_;
        }

        friend bool operator>(time_point a, time_point b) noexcept {
            return a.ticks_ > b.ticks_;
        }

        friend bool operator<=(time_point a, time_point b) noexcept {
            return a.ticks_ <= b.ticks_;
        }

        friend bool operator>=(time_point a, time_point b) noexcept {
            return a.ticks_ >= b.ticks_;
        }

    private:
        // Ticks since Jan 1, 1601 (UTC)
        std::uint64_t ticks_;
    };

    static time_point now() noexcept;
};

} // namespace unifex::win32

#include <unifex/detail/epilogue.hpp>
