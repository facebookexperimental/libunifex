/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <thread>

#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/transform.hpp>
#include <unifex/single_thread_context.hpp>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
struct TaskSchedulerAffinityTest : testing::Test {
  single_thread_context thread_ctx;
};

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<std::pair<std::thread::id, std::thread::id>> child(Scheduler s) {
  auto that_id =
      co_await transform(schedule(s), []{ return std::this_thread::get_id(); });
  // Should have automatically transitioned back to the original thread:
  auto this_id = std::this_thread::get_id();
  co_return std::make_pair(this_id, that_id);
}

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<std::thread::id> inner(Scheduler s) {
  // Transition to the scheduler's context:
  co_await schedule(s);
  // Should return the new context
  co_return std::this_thread::get_id();
}

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<std::pair<std::thread::id, std::thread::id>> outer(Scheduler s) {
  // Call a nested coroutine that transitions context:
  auto that_id = co_await inner(s);
  // Should have automatically transitioned back to the correct context
  auto this_id = std::this_thread::get_id();
  co_return std::make_pair(this_id, that_id);
}
} // anonymous namespace

TEST_F(TaskSchedulerAffinityTest, TransformSenderOnSeparateThread) {
  if(auto opt = sync_wait(child(thread_ctx.get_scheduler()))) {
    auto [this_id, that_id] = *opt;
    ASSERT_EQ(this_id, std::this_thread::get_id());    
    ASSERT_EQ(that_id, thread_ctx.get_thread_id());    
  } else {
    ADD_FAILURE() << "child coroutine completed unexpectedly";
  }
}

TEST_F(TaskSchedulerAffinityTest, InlineThreadHopInCoroutine) {
  if(auto opt = sync_wait(outer(thread_ctx.get_scheduler()))) {
    auto [this_id, that_id] = *opt;
    ASSERT_EQ(this_id, std::this_thread::get_id());    
    ASSERT_EQ(that_id, thread_ctx.get_thread_id());    
  } else {
    ADD_FAILURE() << "child coroutine completed unexpectedly";
  }
}

#endif // !UNIFEX_NO_COROUTINES
