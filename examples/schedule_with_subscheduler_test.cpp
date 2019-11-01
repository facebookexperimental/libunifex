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
#include <unifex/schedule_with_subscheduler.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/transform.hpp>

using namespace unifex;

int main() {
  timed_single_thread_context context;
  auto scheduler = context.get_scheduler();

  std::optional<bool> result = sync_wait(transform(
      cpo::schedule_with_subscheduler(scheduler),
      [&](auto subScheduler) noexcept { return subScheduler == scheduler; }));

  if (result.has_value() && result.value()) {
    // Success
    return 0;
  }

  return 1;
}
