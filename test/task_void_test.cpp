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

#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES

#include <atomic>

#include <unifex/static_thread_pool.hpp>
#include <unifex/when_all.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/let_with.hpp>
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
task<void> example(Scheduler s) {
  std::atomic<int> x{42};
  ++x;
  co_await when_all(child(s, x), child(s, x));
  EXPECT_EQ(x.load(), 45);
}

TEST(TaskVoid, WhenAll) {
  // A work-stealing thread pool with two worker threads:
  static_thread_pool context(2);

  // Take a handle to the thread pool for scheduling work:
  auto sched = context.get_scheduler();

  auto task = example(sched);
  sync_wait(std::move(task));
}

#endif // !UNIFEX_NO_COROUTINES
