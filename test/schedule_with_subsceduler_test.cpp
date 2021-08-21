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

#include <gtest/gtest.h>

using namespace unifex;

TEST(schedule_with_subscheduler, Smoke) {
  timed_single_thread_context context;
  auto scheduler = context.get_scheduler();

  std::optional<bool> result = sync_wait(then(
      schedule_with_subscheduler(scheduler),
      [&](auto subScheduler) noexcept { return subScheduler == scheduler; }));

  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result.value());
}

TEST(schedule_with_subscheduler, Pipeable) {
  timed_single_thread_context context;
  auto scheduler = context.get_scheduler();

  std::optional<bool> result = scheduler
    | schedule_with_subscheduler()
    | then([&](auto subScheduler) noexcept { 
        return subScheduler == scheduler; 
      })
    | sync_wait();

  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result.value());
}
