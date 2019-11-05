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

#include <unifex/config.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <type_traits>

namespace unifex {

template <typename Scheduler, typename Duration>
struct delayed_scheduler {
  Scheduler scheduler_;
  Duration duration_;

  auto schedule() {
    return cpo::schedule_after(scheduler_, duration_);
  }
};

template <typename Scheduler, typename Duration>
auto delay(Scheduler&& scheduler, Duration&& duration) {
  return delayed_scheduler<
      std::remove_cvref_t<Scheduler>,
      std::remove_cvref_t<Duration>>{(Scheduler &&) scheduler,
                                     (Duration &&) duration};
}

} // namespace unifex
