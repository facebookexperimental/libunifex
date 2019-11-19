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

namespace unifex {

inline constexpr struct schedule_with_subscheduler_cpo {
 private:
  template <typename T>
  struct return_value {
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

  template <typename Scheduler>
  friend auto tag_invoke(schedule_with_subscheduler_cpo, Scheduler&& scheduler)
      -> decltype(transform(
          std::declval<std::invoke_result_t<decltype(cpo::schedule), Scheduler&>>(),
          std::declval<return_value<std::decay_t<Scheduler>>>())) {
    auto&& scheduleOp = cpo::schedule(scheduler);
    return transform(
        static_cast<decltype(scheduleOp)>(scheduleOp),
        return_value<std::decay_t<Scheduler>>{(Scheduler &&) scheduler});
  }

 public:
  template <typename Scheduler>
  auto operator()(Scheduler&& s) const
      noexcept(noexcept(tag_invoke(*this, (Scheduler &&) s)))
          -> decltype(tag_invoke(*this, (Scheduler &&) s)) {
    return tag_invoke(*this, (Scheduler &&) s);
  }
} schedule_with_subscheduler;

} // namespace unifex
