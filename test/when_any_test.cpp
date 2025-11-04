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

#  include <unifex/let_done.hpp>
#  include <unifex/let_value_with_stop_source.hpp>
#  include <unifex/stop_when.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>
#  include <unifex/timed_single_thread_context.hpp>
#  include <unifex/unstoppable.hpp>
#  include <unifex/when_any.hpp>
#  include <gtest/gtest.h>

namespace {

using namespace testing;
using namespace unifex;
using namespace std::chrono_literals;
using namespace std::string_literals;

timed_single_thread_context ctx;

task<void> voidTask(bool& returned, bool wait = true, bool selfCancel = false) {
  if (wait) {
    co_await schedule_after(ctx.get_scheduler(), 500ms);
  }
  if (selfCancel) {
    co_await just_done();
  }
  returned = true;
  co_return;
}

task<int>
intTask(bool& returned, int result, bool wait = true, bool selfCancel = false) {
  co_await voidTask(returned, wait, selfCancel);
  co_return result;
}

auto multivalueTask(
    bool& returned,
    int a,
    std::string b,
    bool wait = true,
    bool selfCancel = false) {
  return sequence(voidTask(returned, wait, selfCancel), just(a, b));
}

auto nonCancellableTask(bool& returned, int value, bool wait = true) {
  return unstoppable(intTask(returned, value, wait));
}

TEST(when_any_test, returnValues) {
  {
    bool returned1{false}, returned2{false};
    auto result{sync_wait(when_any(
        voidTask(returned1, false) | then([]() noexcept { return 1; }),
        voidTask(returned2, true) | then([]() noexcept { return 2; })))};
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(1, *result);
    EXPECT_TRUE(returned1);
    EXPECT_FALSE(returned2);
  }
  {
    bool returned1{false}, returned2{false};
    auto result{sync_wait(
        when_any(intTask(returned1, 1, false), intTask(returned2, 2, true)))};
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(1, *result);
    EXPECT_TRUE(returned1);
    EXPECT_FALSE(returned2);
  }
  {
    bool returned1{false}, returned2{false};
    auto result{sync_wait(when_any(
        multivalueTask(returned1, 1, "a"s, false),
        multivalueTask(returned2, 2, "b"s, true)))};
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(1, std::get<0>(*result));
    EXPECT_EQ("a"s, std::get<1>(*result));
    EXPECT_TRUE(returned1);
    EXPECT_FALSE(returned2);
  }
}

TEST(when_any_test, order) {
  {
    bool returned1{false}, returned2{false};
    auto result{sync_wait(
        when_any(intTask(returned1, 1, true), intTask(returned2, 2, false)))};
    EXPECT_EQ(2, *result);
    EXPECT_FALSE(returned1);
    EXPECT_TRUE(returned2);
  }
  {
    bool returned1{false}, returned2{false};
    auto result{sync_wait(
        when_any(voidTask(returned1, true), voidTask(returned2, false, true)))};
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(returned1);
    EXPECT_FALSE(returned2);
  }
  {
    bool returned1{false}, returned2{false};
    auto result{sync_wait(when_any(
        intTask(returned1, 1, false), intTask(returned2, 2, true, true)))};
    EXPECT_EQ(1, *result);
    EXPECT_TRUE(returned1);
    EXPECT_FALSE(returned2);
  }
  {
    bool returned1{false}, returned2{false};
    auto result{sync_wait(when_any(
        nonCancellableTask(returned1, 1, false),
        nonCancellableTask(returned2, 2, true)))};
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(1, *result);
    EXPECT_TRUE(returned1);
    EXPECT_TRUE(returned2);
  }
}

TEST(when_any_test, cancel) {
  {
    bool returned1{false}, returned2{false};
    auto result{sync_wait(stop_when(
        when_any(voidTask(returned1, true), voidTask(returned2, true)),
        schedule_after(ctx.get_scheduler(), 100ms)))};
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(returned1);
    EXPECT_FALSE(returned2);
  }
  {
    bool returned1{false}, returned2{false};
    auto result{sync_wait(
        let_value_with_stop_source([&returned1, &returned2](auto& stopSource) {
          stopSource.request_stop();
          return when_any(voidTask(returned1), voidTask(returned2));
        }))};
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(returned1);
    EXPECT_FALSE(returned2);
  }
}

}  // namespace

#endif  // !UNIFEX_NO_COROUTINES
