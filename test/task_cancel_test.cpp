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

#include <atomic>

#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/transform.hpp>
#include <unifex/transform_done.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
task<int> foo() {
  co_await stop(); // sends a done signal, unwinds the coroutine stack
  ADD_FAILURE();
  co_return 42;
}

task<int> bar() {
  try {
    co_await foo();
    ADD_FAILURE();
  }
  catch (...) {
    ADD_FAILURE();
  }
  co_return -1;
}

task<inplace_stop_token> get_token_inner() {
  co_return co_await get_stop_token();
}

task<inplace_stop_token> get_token_outer() {
  auto a = co_await get_stop_token();
  auto b = co_await get_token_inner();
  EXPECT_EQ(a, b);
  co_return b;
}

task<void> void_test() {
  co_await stop();
  co_return;
}

bool continuedWhenStopWasNotYetRequested = false;

task<int> test_stop_if_requested(inplace_stop_source& stopSource) {
  co_await stop_if_requested(); // shouldn't stop
  continuedWhenStopWasNotYetRequested = true;
  stopSource.request_stop();
  co_await stop_if_requested(); // should stop
  ADD_FAILURE() << "didn't stop but should have";
  co_return 42;
}

template<typename Sender>
auto done_as_optional(Sender&& sender) {
  using value_type = sender_single_value_result_t<unifex::remove_cvref_t<Sender>>;
  return transform_done(
    transform((Sender&&)sender, [](auto&&... values) {
      return std::optional<value_type>{std::in_place, static_cast<decltype(values)>(values)...};
    }), []() {
      return just(std::optional<value_type>(std::nullopt));
    });
}
} // <anonymous namespace>

TEST(TaskCancel, Cancel) {
  std::optional<int> j = sync_wait(bar());
  EXPECT_TRUE(!j);
}

TEST(TaskCancel, DoneAsOptional) {
  std::optional<std::optional<int>> i = sync_wait(done_as_optional(bar()));
  EXPECT_TRUE(i);
  EXPECT_TRUE(!*i);
}

TEST(TaskCancel, VoidTask) {
  std::optional<unit> i = sync_wait(void_test());
  EXPECT_TRUE(!i);
}

TEST(TaskCancel, PropagatesStopToken) {
  inplace_stop_source stopSource;
  std::optional<inplace_stop_token> i =
    sync_wait(
      with_query_value(
        get_token_outer(),
        get_stop_token,
        stopSource.get_token()));
  EXPECT_TRUE(i);
  EXPECT_EQ(*i, stopSource.get_token());
}

TEST(TaskCancel, StopIfRequested) {
  inplace_stop_source stopSource;
  std::optional<int> i =
    sync_wait(
      with_query_value(
        test_stop_if_requested(stopSource),
        get_stop_token,
        stopSource.get_token()));
  EXPECT_TRUE(!i);
  EXPECT_TRUE(continuedWhenStopWasNotYetRequested);
}

#endif // !UNIFEX_NO_COROUTINES
