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

#include <unifex/any_scheduler.hpp>

#include <unifex/inline_scheduler.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/then.hpp>
#include <unifex/sync_wait.hpp>

#include <gtest/gtest.h>

using namespace unifex;

TEST(AnySchedulerTest, SatisfiedConcept) {
  EXPECT_TRUE(scheduler<any_scheduler>);
}

TEST(AnySchedulerTest, EqualityComparable) {
  any_scheduler sched1 = inline_scheduler{};
  any_scheduler sched2 = inline_scheduler{};
  EXPECT_EQ(sched1, sched2);
  EXPECT_FALSE(sched1 != sched2);

  single_thread_context ctx1;
  sched1 = ctx1.get_scheduler();
  EXPECT_NE(sched1, sched2);
  EXPECT_TRUE(sched1 != sched2);

  single_thread_context ctx2;
  sched2 = ctx2.get_scheduler();
  EXPECT_NE(sched1, sched2);
  EXPECT_TRUE(sched1 != sched2);
}

TEST(AnySchedulerTest, Schedule) {
  any_scheduler sched = inline_scheduler{};
  int i = 0;
  sync_wait(then(schedule(sched), [&]{ ++i; }));
  EXPECT_EQ(i, 1);
}

TEST(AnySchedulerRefTest, Schedule) {
  EXPECT_TRUE(scheduler<any_scheduler_ref>);
  inline_scheduler sched{};
  any_scheduler_ref schedRef = sched;
  int i = 0;
  sync_wait(then(schedule(schedRef), [&]{ ++i; }));
  EXPECT_EQ(i, 1);
}
