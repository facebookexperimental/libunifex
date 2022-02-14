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

#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES

#include <unifex/just.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/when_all.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <chrono>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono_literals;

TEST(awaitable_senders, non_void) {
  auto makeTask = [&]() -> task<int> {
    co_return co_await just(42);
  };

  std::optional<int> answer =
      sync_wait(makeTask());

  EXPECT_TRUE(answer.has_value());
  EXPECT_EQ(42, *answer);
}

TEST(awaitable_senders, void) {
  // HACK: ideally would be task<void> once that specialisation has been added.
  auto makeTask = [&]() -> task<unifex::unit> {
    co_await just();
    co_return unifex::unit{};
  };

  std::optional<unifex::unit> answer =
      sync_wait(makeTask());

  EXPECT_TRUE(answer.has_value());
}

TEST(awaitable_senders, task_cancellation) {
  timed_single_thread_context ctx;
  auto sched = ctx.get_scheduler();
  sync_wait(
    stop_when(
      [&]() -> task<int> {
        co_await schedule_after(sched, 500ms);
        ADD_FAILURE();
        co_return -1;
      }(),
      schedule_after(sched, 5ms)));
}

TEST(awaitable_senders, await_multi_value_sender) {
  std::optional<int> result = sync_wait([]() -> task<int> {
    auto [a, b] = co_await just(10, 42);
    EXPECT_EQ(10, a);
    EXPECT_EQ(42, b);
    co_return a + b;
  }());

  EXPECT_EQ(52, result);
}

#endif  // UNIFEX_NO_COROUTINES
