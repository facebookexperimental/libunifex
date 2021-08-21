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
#include <unifex/schedule_with_subscheduler.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/then.hpp>

using namespace unifex;

int main() {
  timed_single_thread_context context;
  auto schedr = context.get_scheduler();

  std::optional<bool> result = sync_wait(then(
      schedule_with_subscheduler(schedr),
      [&](auto subScheduler) noexcept { return subScheduler == schedr; }));

  if (result.has_value() && result.value()) {
    // Success
    return 0;
  }

  return 1;
}
