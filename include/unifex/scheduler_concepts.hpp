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

#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>

#include <type_traits>
#include <exception>

namespace unifex {

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

  struct schedule_sender;

  constexpr schedule_sender operator()() const noexcept;
} schedule;

inline constexpr struct get_scheduler_cpo {
  template <typename Context>
  auto operator()(const Context &context) const noexcept
      -> tag_invoke_result_t<get_scheduler_cpo, const Context &> {
    static_assert(is_nothrow_tag_invocable_v<get_scheduler_cpo, const Context &>);
    static_assert(std::is_invocable_v<
                  decltype(schedule),
                  tag_invoke_result_t<get_scheduler_cpo, const Context &>>);
    return tag_invoke(*this, context);
  }
} get_scheduler;

struct schedule_cpo::schedule_sender {
  template<
    template<typename...> class Variant,
    template<typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template<template<typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  template <
    typename Receiver,
    typename Scheduler =
      std::decay_t<std::invoke_result_t<decltype(get_scheduler), const Receiver&>>,
    typename ScheduleSender = std::invoke_result_t<decltype(schedule), Scheduler&>>
  friend auto tag_invoke(tag_t<connect>, schedule_sender, Receiver &&r)
      -> operation_t<ScheduleSender, Receiver> {
    auto scheduler = get_scheduler(std::as_const(r));
    return connect(schedule(scheduler), (Receiver &&) r);
  }
};

inline constexpr schedule_cpo::schedule_sender schedule_cpo::operator()() const noexcept {
  return {};
}

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

  template<typename Duration>
  class schedule_after_sender {
  public:
    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template<template<typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    explicit schedule_after_sender(Duration d)
    : duration_(d)
    {}

  private:
    friend schedule_after_cpo;

    template<
      typename Receiver,
      typename Scheduler =
        std::decay_t<std::invoke_result_t<decltype(get_scheduler), const Receiver&>>,
      typename ScheduleAfterSender =
        std::invoke_result_t<schedule_after_cpo, Scheduler&, const Duration&>>
    friend auto tag_invoke(tag_t<connect>, const schedule_after_sender& s, Receiver&& r)
        -> operation_t<ScheduleAfterSender, Receiver> {
      auto scheduler = get_scheduler(std::as_const(r));
      return connect(schedule_after_cpo{}(scheduler, std::as_const(s.duration_)), (Receiver&&)r);
    }

    Duration duration_;
  };

  template<typename Duration>
  constexpr schedule_after_sender<Duration> operator()(Duration d) const {
    return schedule_after_sender<Duration>{std::move(d)};
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

} // namespace unifex
