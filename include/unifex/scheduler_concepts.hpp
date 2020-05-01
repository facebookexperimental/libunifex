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

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _schedule {
  struct sender;
  inline const struct _fn {
  private:
    template <typename Scheduler>
    using _schedule_member_result_t =
      decltype(UNIFEX_DECLVAL(Scheduler).schedule());
    template <typename Scheduler>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, Scheduler>,
        meta_tag_invoke_result<_fn>,
        meta_quote1<_schedule_member_result_t>>::template apply<Scheduler>;
  public:
    template(typename Scheduler)
      (requires tag_invocable<_fn, Scheduler>)
    constexpr auto operator()(Scheduler&& s) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Scheduler>)
        -> _result_t<Scheduler> {
      return tag_invoke(_fn{}, static_cast<Scheduler&&>(s));
    }
    template(typename Scheduler)
      (requires (!tag_invocable<_fn, Scheduler>))
    constexpr auto operator()(Scheduler&& s) const noexcept(
        noexcept(static_cast<Scheduler&&>(s).schedule()))
        -> _result_t<Scheduler> {
      return static_cast<Scheduler&&>(s).schedule();
    }

    constexpr sender operator()() const noexcept;
  } schedule{};
} // namespace _schedule
using _schedule::schedule;

namespace _get_scheduler {
  inline const struct _fn {
    template <typename Context>
    auto operator()(const Context &context) const noexcept
        -> tag_invoke_result_t<_fn, const Context &> {
      static_assert(is_nothrow_tag_invocable_v<_fn, const Context &>);
      static_assert(is_callable_v<
                    decltype(schedule),
                    tag_invoke_result_t<_fn, const Context &>>);
      return tag_invoke(*this, context);
    }
  } get_scheduler{};
} // namespace _get_scheduler
using _get_scheduler::get_scheduler;

struct _schedule::sender {
  template <
    template <typename...> class Variant,
    template <typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  template <
    typename Receiver,
    typename Scheduler =
      std::decay_t<callable_result_t<decltype(get_scheduler), const Receiver&>>,
    typename ScheduleSender = callable_result_t<decltype(schedule), Scheduler&>>
  friend auto tag_invoke(tag_t<connect>, sender, Receiver &&r)
      -> connect_result_t<ScheduleSender, Receiver> {
    auto scheduler = get_scheduler(std::as_const(r));
    return connect(schedule(scheduler), (Receiver &&) r);
  }
};

inline constexpr _schedule::sender _schedule::_fn::operator()() const noexcept {
  return {};
}

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

  template <typename Duration>
  class _sender<Duration>::type {
  public:
    template <template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    explicit type(Duration d)
      : duration_(d)
    {}

  private:
    friend _fn;

    template <
      typename Receiver,
      typename Scheduler =
        std::decay_t<callable_result_t<decltype(get_scheduler), const Receiver&>>,
      typename ScheduleAfterSender =
        callable_result_t<_fn, Scheduler&, const Duration&>>
    friend auto tag_invoke(tag_t<connect>, const type& s, Receiver&& r)
        -> connect_result_t<ScheduleAfterSender, Receiver> {
      auto scheduler = get_scheduler(std::as_const(r));
      return connect(schedule_after(scheduler, std::as_const(s.duration_)), (Receiver&&) r);
    }

    Duration duration_;
  };
} // namespace _schedule_after
using _schedule_after::schedule_after;

namespace _schedule_at {
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
  } schedule_at {};
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

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
