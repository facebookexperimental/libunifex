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

#include <atomic>

#include <unifex/static_thread_pool.hpp>
#include <unifex/when_all.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/task.hpp>

#include <gtest/gtest.h>

using namespace unifex;

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<void> child(Scheduler s, std::atomic<int>& x) {
  co_await schedule(s);
  ++x;
}

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<void> example(Scheduler s, std::atomic<int>& x) {
  ++x;
  co_await when_all(child(s, x), child(s, x));
}

TEST(TaskVoid, WhenAll) {
  std::atomic<int> x{42};
  // A work-stealing thread pool with two worker threads:
  static_thread_pool context(2);

  // Take a handle to the thread pool for scheduling work:
  auto sched = context.get_scheduler();

  auto task = example(sched, x);
  sync_wait(std::move(task));
  EXPECT_EQ(x.load(), 45);
}

#endif // !UNIFEX_NO_COROUTINES
