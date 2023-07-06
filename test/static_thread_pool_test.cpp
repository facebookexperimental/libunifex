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
#include <unifex/static_thread_pool.hpp>

#include <unifex/just.hpp>
#include <unifex/let_done.hpp>
#include <unifex/let_error.hpp>
#include <unifex/on.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>

#include <atomic>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
template <typename Scheduler, typename F>
auto run_on(Scheduler&& s, F&& func) {
  return then(schedule((Scheduler &&) s), (F &&) func);
}
} // anonymous namespace

TEST(StaticThreadPool, Smoke) {
  static_thread_pool tpContext;
  auto tp = tpContext.get_scheduler();
  std::atomic<int> x = 0;

  sync_wait(when_all(
      run_on(
          tp,
          [&] {
            ++x;
            std::printf("task 1\n");
          }),
      run_on(
          tp,
          [&] {
            ++x;
            std::printf("task 2\n");
          }),
      run_on(tp, [&] {
        ++x;
        std::printf("task 3\n");
      })));

  sync_wait(on(tp, just()));

  EXPECT_EQ(x, 3);
}

TEST(StaticThreadPool, ScheduleCancelationThreadSafety) {
    static_thread_pool tpContext;
    auto sch = tpContext.get_scheduler();

    unifex::sync_wait(unifex::repeat_effect_until(
        unifex::let_done(
            unifex::stop_when(
                unifex::repeat_effect(unifex::schedule(sch)),
                unifex::schedule(sch)),
            [] { return unifex::just(); }),
        [n=0]() mutable noexcept { return n++ == 1000; }));

    unifex::sync_wait(unifex::repeat_effect_until(
        unifex::let_done(
          unifex::let_error(
            unifex::stop_when(
                unifex::repeat_effect(unifex::schedule(sch)),
                unifex::schedule(sch)),
            [](auto&&) { return unifex::just(); }),
          [] { return unifex::just(); }),
        [n=0]() mutable noexcept { return n++ == 1000; }));

    unifex::sync_wait(unifex::repeat_effect_until(
        unifex::let_error(
          unifex::let_done(
            unifex::stop_when(
                unifex::repeat_effect(unifex::schedule(sch)),
                unifex::schedule(sch)),
            [] { return unifex::just(); }),
          [](auto&&) { return unifex::just(); }),
        [n=0]() mutable noexcept { return n++ == 1000; }));
}
