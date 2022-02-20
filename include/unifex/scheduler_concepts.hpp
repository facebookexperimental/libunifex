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

#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>

#include <type_traits>
#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _schedule {
  struct _fn;

  template <typename Scheduler>
  UNIFEX_CONCEPT_FRAGMENT( //
    _with_tag_invoke_helper, //
      requires () (0) &&
      tag_invocable<_fn, Scheduler> &&
      sender<tag_invoke_result_t<_fn, Scheduler>>);
  template <typename Scheduler>
  UNIFEX_CONCEPT //
    _with_tag_invoke = //
      UNIFEX_FRAGMENT(_schedule::_with_tag_invoke_helper, Scheduler);

  template <typename Scheduler>
  using _member_schedule_result_t =
    decltype(UNIFEX_DECLVAL(Scheduler).schedule());

  template <typename Scheduler>
  UNIFEX_CONCEPT_FRAGMENT( //
    _has_member_schedule_, //
      requires() (0) &&
      sender<_member_schedule_result_t<Scheduler>>);
  template <typename Scheduler>
  UNIFEX_CONCEPT //
    _with_member_schedule = //
      UNIFEX_FRAGMENT(_schedule::_has_member_schedule_, Scheduler);

  struct sender;
  inline const struct _fn {
  private:
    template <typename Scheduler>
    static auto _select() noexcept {
      if constexpr (_with_tag_invoke<Scheduler>) {
        return meta_tag_invoke_result<_fn>{};
      } else if constexpr (_with_member_schedule<Scheduler>) {
        return meta_quote1<_member_schedule_result_t>{};
      } else {
        return type_always<void>{};
      }
    }
    template <typename Scheduler>
    using _result_t =
        typename decltype(_fn::_select<Scheduler>())::template apply<Scheduler>;
  public:
    template(typename Scheduler)
      (requires _with_tag_invoke<Scheduler>)
    constexpr auto operator()(Scheduler&& s) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Scheduler>)
        -> _result_t<Scheduler> {
      return tag_invoke(_fn{}, static_cast<Scheduler&&>(s));
    }
    template(typename Scheduler)
      (requires (!_with_tag_invoke<Scheduler>) AND
        _with_member_schedule<Scheduler>)
    constexpr auto operator()(Scheduler&& s) const noexcept(
        noexcept(static_cast<Scheduler&&>(s).schedule()))
        -> _result_t<Scheduler> {
      return static_cast<Scheduler&&>(s).schedule();
    }

    constexpr sender operator()() const noexcept;
  } schedule{};
} // namespace _schedule
using _schedule::schedule;

template <typename S>
using schedule_result_t = decltype(schedule(UNIFEX_DECLVAL(S&&)));

// Define the scheduler concept without the macros for better diagnostics
#if UNIFEX_CXX_CONCEPTS
template <typename S>
concept //
  scheduler = //
    copy_constructible<remove_cvref_t<S>> &&
    equality_comparable<remove_cvref_t<S>> &&
    requires(S&& s) {
      schedule((S&&) s);
    };
#else
template <typename S>
UNIFEX_CONCEPT_FRAGMENT( //
  _scheduler,
    requires(S&& s) (
      schedule((S&&) s)
    ));
template <typename S>
UNIFEX_CONCEPT //
  scheduler = //
    copy_constructible<remove_cvref_t<S>> &&
    equality_comparable<remove_cvref_t<S>> &&
    UNIFEX_FRAGMENT(unifex::_scheduler, S);
#endif

namespace _get_scheduler {
  inline const struct _fn {
    template (typename SchedulerProvider)
        (requires tag_invocable<_fn, const SchedulerProvider&>)
    auto operator()(const SchedulerProvider& context) const noexcept
        -> tag_invoke_result_t<_fn, const SchedulerProvider&> {
      static_assert(is_nothrow_tag_invocable_v<_fn, const SchedulerProvider&>);
      static_assert(
          scheduler<tag_invoke_result_t<_fn, const SchedulerProvider&>>);
      return tag_invoke(*this, context);
    }
  } get_scheduler{};
} // namespace _get_scheduler
using _get_scheduler::get_scheduler;

template <typename SchedulerProvider>
using get_scheduler_result_t =
    decltype(get_scheduler(UNIFEX_DECLVAL(SchedulerProvider&&)));

// Define the scheduler concept without the macros for better diagnostics
#if UNIFEX_CXX_CONCEPTS
template <typename SP>
concept //
  scheduler_provider = //
    requires(SP&& sp) {
      get_scheduler((SP&&) sp);
    };
#else
template <typename SP>
UNIFEX_CONCEPT_FRAGMENT( //
  _scheduler_provider,
    requires(SP&& sp) (
      get_scheduler((SP&&) sp)
    ));
template <typename SP>
UNIFEX_CONCEPT //
  scheduler_provider = //
    UNIFEX_FRAGMENT(unifex::_scheduler_provider, SP);
#endif

namespace _schedule {
struct sender {
  template <
    template <typename...> class Variant,
    template <typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  template(typename Receiver)
    (requires receiver<Receiver>)
  friend auto tag_invoke(tag_t<connect>, sender, Receiver &&r)
      -> connect_result_t<
            schedule_result_t<
                get_scheduler_result_t<const remove_cvref_t<Receiver>&>>,
            Receiver> {
    auto scheduler = get_scheduler(std::as_const(r));
    return connect(schedule(std::move(scheduler)), (Receiver &&) r);
  }
};

inline constexpr sender _fn::operator()() const noexcept {
  return {};
}
} // namespace _schedule

namespace _schedule_after {
  template <typename Duration>
  struct _sender {
    class type;
  };
  template <typename Duration>
  using sender = typename _sender<Duration>::type;

  inline const struct _fn {
  private:
    template <typename TimeScheduler, typename Duration>
    using _schedule_after_member_result_t =
      decltype(UNIFEX_DECLVAL(TimeScheduler).schedule_after(UNIFEX_DECLVAL(Duration)));
    template <typename TimeScheduler, typename Duration>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, TimeScheduler, Duration>,
        meta_tag_invoke_result<_fn>,
        meta_quote2<_schedule_after_member_result_t>>
          ::template apply<TimeScheduler, Duration>;
  public:
    template(typename TimeScheduler, typename Duration)
      (requires tag_invocable<_fn, TimeScheduler, Duration>)
    constexpr auto operator()(TimeScheduler&& s, Duration&& d) const
        noexcept(is_nothrow_tag_invocable_v<_fn, TimeScheduler, Duration>)
        -> _result_t<TimeScheduler, Duration> {
      return tag_invoke(*this, (TimeScheduler &&) s, (Duration &&) d);
    }

    template(typename TimeScheduler, typename Duration)
      (requires (!tag_invocable<_fn, TimeScheduler, Duration>))
    constexpr auto operator()(TimeScheduler&& s, Duration&& d) const noexcept(
        noexcept(static_cast<TimeScheduler&&>(s).schedule_after((Duration &&) d)))
        -> _result_t<TimeScheduler, Duration> {
      return static_cast<TimeScheduler&&>(s).schedule_after((Duration &&) d);
    }

    template <typename Duration>
    constexpr sender<Duration> operator()(Duration d) const {
      return sender<Duration>{std::move(d)};
    }
  } schedule_after{};

  template <typename TimeScheduler, typename Duration>
  using schedule_after_result_t =
      decltype(schedule_after(UNIFEX_DECLVAL(TimeScheduler&&), UNIFEX_DECLVAL(Duration&&)));

  template <typename Duration>
  class _sender<Duration>::type {
  public:
    template <template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    explicit type(Duration d)
      : duration_(d)
    {}

  private:
    friend _fn;

    template(typename Receiver)
      (requires receiver<Receiver>)
    friend auto tag_invoke(tag_t<connect>, const type& s, Receiver&& r)
        -> connect_result_t<
            schedule_after_result_t<std::decay_t<
                get_scheduler_result_t<const remove_cvref_t<Receiver>&>>&,
                const Duration&>,
            Receiver> {
      auto scheduler = get_scheduler(std::as_const(r));
      return connect(schedule_after(scheduler, std::as_const(s.duration_)), (Receiver&&) r);
    }

    Duration duration_;
  };
} // namespace _schedule_after
using _schedule_after::schedule_after;

namespace _schedule_at {
  template <typename TimePoint>
  struct _sender {
    class type;
  };
  template <typename TimePoint>
  using sender = typename _sender<TimePoint>::type;

  inline const struct _fn {
  private:
    template <typename TimeScheduler, typename TimePoint>
    using _schedule_at_member_result_t =
      decltype(UNIFEX_DECLVAL(TimeScheduler).schedule_at(UNIFEX_DECLVAL(TimePoint)));
    template <typename TimeScheduler, typename TimePoint>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, TimeScheduler, TimePoint>,
        meta_tag_invoke_result<_fn>,
        meta_quote2<_schedule_at_member_result_t>>
          ::template apply<TimeScheduler, TimePoint>;
  public:
    template(typename TimeScheduler, typename TimePoint)
      (requires tag_invocable<_fn, TimeScheduler, TimePoint>)
    constexpr auto operator()(TimeScheduler&& s, TimePoint&& tp) const
        noexcept(is_nothrow_tag_invocable_v<_fn, TimeScheduler, TimePoint>)
        -> _result_t<TimeScheduler, TimePoint> {
      return tag_invoke(*this, (TimeScheduler &&) s, (TimePoint &&) tp);
    }

    template(typename TimeScheduler, typename TimePoint)
      (requires (!tag_invocable<_fn, TimeScheduler, TimePoint>))
    constexpr auto operator()(TimeScheduler&& s, TimePoint&& tp) const noexcept(
        noexcept(static_cast<TimeScheduler&&>(s).schedule_at((TimePoint &&) tp)))
        -> _result_t<TimeScheduler, TimePoint> {
      return static_cast<TimeScheduler&&>(s).schedule_at((TimePoint &&) tp);
    }

    template <typename TimePoint>
    constexpr sender<TimePoint> operator()(TimePoint tp) const {
      return sender<TimePoint>{std::move(tp)};
    }
  } schedule_at {};

  template <typename TimeScheduler, typename TimePoint>
  using schedule_at_result_t =
      decltype(schedule_at(
          UNIFEX_DECLVAL(TimeScheduler&&),
          UNIFEX_DECLVAL(TimePoint&&)));

  template <typename TimePoint>
  class _sender<TimePoint>::type {
  public:
    template <template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    explicit type(TimePoint tp)
      : time_point_(tp)
    {}

  private:
    friend _fn;

    template(typename Receiver)
      (requires receiver<Receiver>)
    friend auto tag_invoke(tag_t<connect>, const type& s, Receiver&& r)
        -> connect_result_t<
            schedule_at_result_t<std::decay_t<
                get_scheduler_result_t<const remove_cvref_t<Receiver>&>>&,
                const TimePoint&>,
            Receiver> {
      auto scheduler = get_scheduler(std::as_const(r));
      return connect(schedule_at(scheduler, std::as_const(s.time_point_)), (Receiver&&) r);
    }

    TimePoint time_point_;
  };
} // namespace _schedule_at
using _schedule_at::schedule_at;

namespace _now {
  inline const struct _fn {
  private:
    template <typename TimeScheduler>
    using _now_member_result_t =
      decltype(UNIFEX_DECLVAL(TimeScheduler).now());
    template <typename TimeScheduler>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, TimeScheduler>,
        meta_tag_invoke_result<_fn>,
        meta_quote1<_now_member_result_t>>::template apply<TimeScheduler>;
  public:
    template(typename TimeScheduler)
      (requires tag_invocable<_fn, TimeScheduler>)
    constexpr auto operator()(TimeScheduler&& s) const
        noexcept(is_nothrow_tag_invocable_v<_fn, TimeScheduler>)
        -> _result_t<TimeScheduler> {
      return tag_invoke(*this, (TimeScheduler &&) s);
    }

    template(typename TimeScheduler)
      (requires (!tag_invocable<_fn, TimeScheduler>))
    constexpr auto operator()(TimeScheduler&& s) const noexcept(
        noexcept(static_cast<TimeScheduler&&>(s).now()))
        -> _result_t<TimeScheduler> {
      return static_cast<TimeScheduler&&>(s).now();
    }
  } now {};
} // namespace _now
using _now::now;

namespace _current {
  inline constexpr struct _scheduler {
    auto schedule() const noexcept {
        return unifex::schedule();
    }
    template <typename Duration>
    auto schedule_after(Duration d) const {
        return unifex::schedule_after(std::move(d));
    }
    template <typename TimePoint>
    auto schedule_at(TimePoint tp) const {
        return unifex::schedule_at(std::move(tp));
    }
    friend constexpr bool operator==(_scheduler, _scheduler) noexcept {
        return true;
    }
    friend constexpr bool operator!=(_scheduler, _scheduler) noexcept {
        return false;
    }
  } current_scheduler{};
}
using _current::current_scheduler;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
