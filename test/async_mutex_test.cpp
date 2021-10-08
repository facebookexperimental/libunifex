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

#  include <unifex/async_mutex.hpp>
#  include <unifex/scheduler_concepts.hpp>
#  include <unifex/single_thread_context.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>
#  include <unifex/when_all.hpp>

#  include <gtest/gtest.h>

using namespace unifex;

TEST(async_mutex, multiple_threads) {
#if !defined(UNIFEX_TEST_LIMIT_ASYNC_MUTEX_ITERATIONS)
  constexpr int iterations = 100'000;
#else
  constexpr int iterations = UNIFEX_TEST_LIMIT_ASYNC_MUTEX_ITERATIONS;
#endif

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

  sync_wait(when_all(
      makeTask(ctx1.get_scheduler()),
      makeTask(ctx2.get_scheduler())));

  EXPECT_EQ(2 * iterations, sharedState);
}

#endif  // UNIFEX_NO_COROUTINES
