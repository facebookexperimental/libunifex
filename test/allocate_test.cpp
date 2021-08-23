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

#include <unifex/allocate.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <array>
#include <cstdio>

#include <gtest/gtest.h>

using namespace unifex;

TEST(Allocate, Smoke) {
  single_thread_context threadContext;

  auto thread = threadContext.get_scheduler();
  int count = 0;

  sync_wait(allocate(then(
      schedule(thread), [&] { ++count; })));

  EXPECT_EQ(count, 1);
}

TEST(Allocate, Pipeable) {
  single_thread_context threadContext;

  auto thread = threadContext.get_scheduler();
  int count = 0;

  schedule(thread)
    | then([&] { ++count; })
    | allocate()
    | sync_wait();

  EXPECT_EQ(count, 1);
}
