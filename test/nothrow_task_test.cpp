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
#include <unifex/task.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/just_error.hpp>
#include <unifex/stop_if_requested.hpp>


#include <gtest/gtest.h>

using namespace unifex;

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
nothrow_task<void> child(Scheduler s, std::atomic<int>& x) {
  co_await unifex::then(schedule(s), []() noexcept {});
  ++x;
}

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
nothrow_task<void> example(Scheduler s, std::atomic<int>& x) {
  ++x;
  co_await when_all(child(s, x), child(s, x));
}

nothrow_task<void> nothrowThrowsException() {
  throw std::exception{};
  co_return;
}

nothrow_task<void> nothrowJustError() {
  co_await unifex::just_error(std::make_exception_ptr(42));
}

task<void> nothrowTaskBody() {
  co_await nothrowJustError();
}

task<int> foo() {
  co_await stop(); // sends a done signal, unwinds the coroutine stack
  ADD_FAILURE();
  co_return 42;
}

nothrow_task<int> bar() {
  try {
    co_await foo();
    ADD_FAILURE();
  }
  catch (...) {
    ADD_FAILURE();
  }
  co_return -1;
}

nothrow_task<bool> nothrowTryCatch() {
  try {
    throw std::exception{};
  } catch (...) {
    co_return true;
  }
}

// Test that after a co_await schedule(), the coroutine's current
// scheduler has NOT changed. Note that this behavior is different
// for nothrow_task compared to regular task
UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
nothrow_task<bool> test_current_scheduler(Scheduler s) {
  auto before = co_await current_scheduler();
  co_await unifex::then(schedule(s), []() noexcept {});
  auto after = co_await current_scheduler();
  co_return before == after;
}

// Test that after a co_await schedule(), the coroutine's current
// scheduler is NOT inherited by child tasks. Note that this behavior
// is different for nothrow_task compared to regular task
UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
nothrow_task<std::pair<bool, std::thread::id>> test_current_scheduler_is_inherited_impl(Scheduler s) {
  any_scheduler s2 = co_await current_scheduler();
  bool sameScheduler = (s2 != s);
  co_return std::make_pair(sameScheduler, std::this_thread::get_id());
}

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
nothrow_task<std::pair<bool, std::thread::id>> test_current_scheduler_is_inherited(Scheduler s) {
  co_await unifex::then(schedule(s), []() noexcept {});
  co_return co_await test_current_scheduler_is_inherited_impl(s);
}

TEST(NothrowTask, WhenAll) {
  std::atomic<int> x{42};
  // A work-stealing thread pool with two worker threads:
  static_thread_pool context(2);

  // Take a handle to the thread pool for scheduling work:
  auto sched = context.get_scheduler();

  auto task = example(sched, x);
  sync_wait(std::move(task));
  EXPECT_EQ(x.load(), 45);
}

TEST(NothrowTaskDeathTest, ExceptionCausesProgramTermination) {
  ASSERT_DEATH(sync_wait(nothrowThrowsException()), "");
}

TEST(NothrowTaskDeathTest, JustErrorCausesProgramTermination) {
  ASSERT_DEATH(sync_wait(nothrowJustError()), "");
}

TEST(NothrowTaskSchedulerAffinityTest, CurrentSchedulerTest) {
  single_thread_context thread_ctx;
  if(auto opt = sync_wait(test_current_scheduler(thread_ctx.get_scheduler()))) {
    ASSERT_TRUE(opt.has_value());
    EXPECT_TRUE(*opt);
  } else {
    ADD_FAILURE() << "child coroutine completed unexpectedly";
  }
}

TEST(NothrowTaskSchedulerAffinityTest, CurrentSchedulerIsInheritedTest) {
  single_thread_context thread_ctx;
  if(auto opt = sync_wait(test_current_scheduler_is_inherited(thread_ctx.get_scheduler()))) {
    ASSERT_TRUE(opt.has_value());
    auto [differentScheduler, thread_id] = *opt;
    EXPECT_TRUE(differentScheduler);
    EXPECT_NE(thread_id, thread_ctx.get_thread_id());
  } else {
    ADD_FAILURE() << "child coroutine completed unexpectedly";
  }
}

TEST(NothrowTaskDeathTest, NothrowTaskNestedInTaskStillTerminates) {
  ASSERT_DEATH(sync_wait(nothrowTaskBody()), "");
}

TEST(NothrowTaskTest, BasicCancellationStillWorks) {
  std::optional<int> j = sync_wait(bar());
  EXPECT_TRUE(!j);
}

TEST(NothrowTaskTest, NothrowDoesNotTerminateWithTryCatch) {
  EXPECT_TRUE(sync_wait(nothrowTryCatch()));
}

#endif // !UNIFEX_NO_COROUTINES
