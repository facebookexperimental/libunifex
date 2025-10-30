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

#  include <unifex/async_manual_reset_event.hpp>
#  include <unifex/let_done.hpp>
#  include <unifex/scheduler_concepts.hpp>
#  include <unifex/scope_guard.hpp>
#  include <unifex/single_thread_context.hpp>
#  include <unifex/stop_when.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>
#  include <unifex/timed_single_thread_context.hpp>
#  include <unifex/v2/async_mutex.hpp>
#  include <unifex/when_all.hpp>

#  include <gtest/gtest.h>

using namespace unifex;
using v2::async_mutex;

TEST(async_mutex_v2, multiple_threads) {
#  if !defined(UNIFEX_TEST_LIMIT_ASYNC_MUTEX_ITERATIONS)
  constexpr int iterations = 100'000;
#  else
  constexpr int iterations = UNIFEX_TEST_LIMIT_ASYNC_MUTEX_ITERATIONS;
#  endif

  async_mutex mutex;

  int sharedState = 0;

  auto makeTask = [&](manual_event_loop::scheduler scheduler) -> task<int> {
    for (int i = 0; i < iterations; ++i) {
      co_await mutex.async_lock();
      co_await schedule(scheduler);
      ++sharedState;
      mutex.unlock();
    }
    co_return 0;
  };

  single_thread_context ctx1;
  single_thread_context ctx2;

  sync_wait(
      when_all(makeTask(ctx1.get_scheduler()), makeTask(ctx2.get_scheduler())));

  EXPECT_EQ(2 * iterations, sharedState);
}

using namespace std::chrono_literals;

class async_mutex_v2_test : public ::testing::Test {
public:
  static auto timer() noexcept {
    static timed_single_thread_context timer;
    return timer.get_scheduler();
  }

  static auto maybeCancelled(bool& cancelled) {
    cancelled = false;
    return let_done([&cancelled]() {
      cancelled = true;
      return just();
    });
  }

  task<void> critSecTask(bool& acquired, bool& finished) {
    acquired = false;
    finished = false;
    co_await mutex.async_lock();
    acquired = true;
    scope_guard guard([this]() noexcept { mutex.unlock(); });
    co_await schedule_after(timer(), 500ms);
    finished = true;
  }

  task<void> delayedCritSecTask(bool& acquired, bool& finished) {
    acquired = false;
    finished = false;
    bool cancelled = false;
    co_await (schedule_after(timer(), 200ms) | maybeCancelled(cancelled));
    co_await critSecTask(acquired, finished);
    EXPECT_TRUE(!cancelled || !acquired);  // cancelled implies !acquired
  }

  async_mutex mutex;
};

TEST_F(async_mutex_v2_test, cancel_awaiting) {
  bool first_acquired, first_finished, second_acquired, second_finished,
      second_cancelled;

  sync_wait(when_all(
      critSecTask(first_acquired, first_finished),
      stop_when(
          critSecTask(second_acquired, second_finished) |
              maybeCancelled(second_cancelled),
          schedule_after(timer(), 100ms))));

  EXPECT_TRUE(first_acquired);
  EXPECT_TRUE(first_finished);
  EXPECT_TRUE(second_cancelled);
  EXPECT_FALSE(second_acquired);
  EXPECT_FALSE(second_finished);
}

TEST_F(async_mutex_v2_test, cancel_running) {
  bool first_acquired, first_finished, first_cancelled, second_acquired,
      second_finished;

  sync_wait(when_all(
      stop_when(
          critSecTask(first_acquired, first_finished) |
              maybeCancelled(first_cancelled),
          schedule_after(timer(), 100ms)),
      critSecTask(second_acquired, second_finished)));

  EXPECT_TRUE(first_acquired);
  EXPECT_FALSE(first_finished);
  EXPECT_TRUE(first_cancelled);
  EXPECT_TRUE(second_acquired);
  EXPECT_TRUE(second_finished);
}

TEST_F(async_mutex_v2_test, cancel_awaiting_middle) {
  bool first_acquired, first_finished, second_acquired, second_finished,
      second_cancelled, third_acquired, third_finished;

  sync_wait(when_all(
      critSecTask(first_acquired, first_finished),
      stop_when(
          critSecTask(second_acquired, second_finished) |
              maybeCancelled(second_cancelled),
          schedule_after(timer(), 100ms)),
      critSecTask(third_acquired, third_finished)));

  EXPECT_TRUE(first_acquired);
  EXPECT_TRUE(first_finished);
  EXPECT_TRUE(second_cancelled);
  EXPECT_FALSE(second_acquired);
  EXPECT_FALSE(second_finished);
  EXPECT_TRUE(third_acquired);
  EXPECT_TRUE(third_finished);
}

TEST_F(async_mutex_v2_test, cancel_early) {
  bool first_acquired, first_finished, first_cancelled;

  sync_wait(stop_when(
      delayedCritSecTask(first_acquired, first_finished) |
          maybeCancelled(first_cancelled),
      schedule_after(timer(), 100ms)));

  EXPECT_FALSE(first_acquired);
  EXPECT_FALSE(first_finished);
  EXPECT_TRUE(first_cancelled);
}

TEST_F(async_mutex_v2_test, cancel_awaiting_early) {
  bool first_acquired, first_finished, second_acquired, second_finished,
      second_cancelled;

  sync_wait(when_all(
      critSecTask(first_acquired, first_finished),
      stop_when(
          delayedCritSecTask(second_acquired, second_finished) |
              maybeCancelled(second_cancelled),
          schedule_after(timer(), 100ms))));

  EXPECT_TRUE(first_acquired);
  EXPECT_TRUE(first_finished);
  EXPECT_TRUE(second_cancelled);
  EXPECT_FALSE(second_acquired);
  EXPECT_FALSE(second_finished);
}

#endif  // UNIFEX_NO_COROUTINES
