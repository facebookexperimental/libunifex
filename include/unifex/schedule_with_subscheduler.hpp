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

#include <unifex/scheduler_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/transform.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _subschedule {
  inline constexpr struct _fn {
  private:
    template <typename T>
    struct _return_value {
      struct type {
        T value;

        T operator()() && {
          return std::move(value);
        }

        T operator()() & {
          return value;
        }

        T operator()() const& {
          return value;
        }
      };
    };
    template <typename T>
    using return_value = typename _return_value<std::decay_t<T>>::type;

    template <bool>
    struct _impl {
    template <typename Scheduler>
      auto operator()(Scheduler&& sched) const
          noexcept(is_nothrow_tag_invocable_v<_fn, Scheduler>)
          -> tag_invoke_result_t<_fn, Scheduler> {
        return unifex::tag_invoke(_fn{}, (Scheduler &&) sched);
      }
    };
  public:
    template <typename Scheduler>
    auto operator()(Scheduler&& sched) const
        noexcept(is_nothrow_callable_v<
            _impl<is_tag_invocable_v<_fn, Scheduler>>, Scheduler>)
        -> callable_result_t<
            _impl<is_tag_invocable_v<_fn, Scheduler>>, Scheduler> {
      return _impl<is_tag_invocable_v<_fn, Scheduler>>{}(
          (Scheduler &&) sched);
    }
  } schedule_with_subscheduler{};

  template <>
  struct _fn::_impl<false> {
    template <typename Scheduler>
    auto operator()(Scheduler&& sched) const
        -> decltype(transform(
            std::declval<callable_result_t<decltype(schedule), Scheduler&>>(),
            std::declval<return_value<Scheduler>>())) {
    auto&& scheduleOp = schedule(sched);
    return transform(
        static_cast<decltype(scheduleOp)>(scheduleOp),
        return_value<Scheduler>{(Scheduler &&) sched});
    }
  };
} // namespace _subschedule

using _subschedule::schedule_with_subscheduler;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
