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
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/just.hpp>
#include <unifex/then.hpp>
#include <unifex/let_done.hpp>
#include <unifex/sequence.hpp>
#include <unifex/stop_when.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

TEST(Transform, Smoke) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  int count = 0;

  sync_wait(
        then(
          schedule_after(scheduler, 200ms), 
          [&]{ ++count; }));

  EXPECT_EQ(count, 1);
}

TEST(Pipeable, Transform) {
  int count = 0;

  just()
    | then([&]{ ++count; })
    | sync_wait();

  auto twocount = then([&]{ ++count; }) | then([&]{ ++count; });

  just()
    | then([&]{ ++count; })
    | twocount
    | sync_wait();

  EXPECT_EQ(count, 4);
}
