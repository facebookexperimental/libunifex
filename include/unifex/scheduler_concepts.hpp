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
namespace cpo {

inline constexpr struct schedule_cpo {
  template <typename Scheduler>
  friend constexpr auto tag_invoke(schedule_cpo, Scheduler&& s) noexcept(
      noexcept(static_cast<Scheduler&&>(s).schedule()))
      -> decltype(static_cast<Scheduler&&>(s).schedule()) {
    return static_cast<Scheduler&&>(s).schedule();
  }

  template <typename Scheduler>
  constexpr auto operator()(Scheduler&& s) const
      noexcept(noexcept(tag_invoke(*this, static_cast<Scheduler&&>(s))))
          -> decltype(tag_invoke(*this, static_cast<Scheduler&&>(s))) {
    return tag_invoke(*this, static_cast<Scheduler&&>(s));
  }
} schedule;

inline constexpr struct schedule_after_cpo {
  template <typename TimeScheduler, typename Duration>
  friend constexpr auto
  tag_invoke(schedule_after_cpo, TimeScheduler&& s, Duration&& d) noexcept(
      noexcept(static_cast<TimeScheduler&&>(s).schedule_after((Duration &&) d)))
      -> decltype(static_cast<TimeScheduler&&>(s).schedule_after((Duration &&)
                                                                     d)) {
    return static_cast<TimeScheduler&&>(s).schedule_after((Duration &&) d);
  }

  template <typename TimeScheduler, typename Duration>
  constexpr auto operator()(TimeScheduler&& s, Duration&& d) const noexcept(
      noexcept(tag_invoke(*this, (TimeScheduler &&) s, (Duration &&) d)))
      -> decltype(tag_invoke(*this, (TimeScheduler &&) s, (Duration &&) d)) {
    return tag_invoke(*this, (TimeScheduler &&) s, (Duration &&) d);
  }
} schedule_after;

inline constexpr struct schedule_at_cpo {
  template <typename TimeScheduler, typename TimePoint>
  friend constexpr auto
  tag_invoke(schedule_at_cpo, TimeScheduler&& s, TimePoint&& tp) noexcept(
      noexcept(static_cast<TimeScheduler&&>(s).schedule_at((TimePoint &&) tp)))
      -> decltype(static_cast<TimeScheduler&&>(s).schedule_at((TimePoint &&)
                                                                  tp)) {
    return static_cast<TimeScheduler&&>(s).schedule_at((TimePoint &&) tp);
  }

  template <typename TimeScheduler, typename TimePoint>
  constexpr auto operator()(TimeScheduler&& s, TimePoint&& tp) const noexcept(
      noexcept(tag_invoke(*this, (TimeScheduler &&) s, (TimePoint &&) tp)))
      -> decltype(tag_invoke(*this, (TimeScheduler &&) s, (TimePoint &&) tp)) {
    return tag_invoke(*this, (TimeScheduler &&) s, (TimePoint &&) tp);
  }
} schedule_at;

inline constexpr struct now_cpo {
  template <typename TimeScheduler>
  friend constexpr auto tag_invoke(now_cpo, TimeScheduler&& s) noexcept(
      noexcept(static_cast<TimeScheduler&&>(s).now()))
      -> decltype(static_cast<TimeScheduler&&>(s).now()) {
    return static_cast<TimeScheduler&&>(s).now();
  }

  template <typename TimeScheduler>
  constexpr auto operator()(TimeScheduler&& ex) const
      noexcept(noexcept(tag_invoke(*this, (TimeScheduler &&) ex)))
          -> decltype(tag_invoke(*this, (TimeScheduler &&) ex)) {
    return tag_invoke(*this, (TimeScheduler &&) ex);
  }
} now;

} // namespace cpo
} // namespace unifex
