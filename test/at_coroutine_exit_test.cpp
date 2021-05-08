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

#include <unifex/at_coroutine_exit.hpp>

#include <unifex/task.hpp>
#include <unifex/stop_if_requested.hpp>
#include <unifex/sync_wait.hpp>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
int global = 0;

task<void> test_one_cleanup_action() {
  ++global;
  co_await at_coroutine_exit([]() -> task<void> { global *= 2; co_return; });
  ++global;
}

task<void> test_two_cleanup_actions() {
  ++global;
  co_await at_coroutine_exit([]() -> task<void> { global *= 2; co_return; });
  co_await at_coroutine_exit([]() -> task<void> { global *= global; co_return; });
  ++global;
}

task<void> test_one_cleanup_action_with_stop() {
  ++global;
  co_await at_coroutine_exit([]() -> task<void> { global *= 2; co_return; });
  co_await stop();
  ++global;
}

task<void> test_two_cleanup_actions_with_stop() {
  ++global;
  co_await at_coroutine_exit([]() -> task<void> { global *= 2; co_return; });
  co_await at_coroutine_exit([]() -> task<void> { global += global; co_return; });
  co_await stop();
  ++global;
}

task<void> with_continuation(unifex::task<void> next) {
  co_await std::move(next);
  global *= 3;
}

}

TEST(AtCoroutineExit, OneCleanupAction) {
  global = 0;
  sync_wait(test_one_cleanup_action());
  EXPECT_EQ(global, 4);
}

TEST(AtCoroutineExit, TwoCleanupActions) {
  global = 0;
  sync_wait(test_two_cleanup_actions());
  EXPECT_EQ(global, 8);
}

TEST(AtCoroutineExit, OneCleanupActionWithContinuation) {
  global = 0;
  sync_wait(with_continuation(test_one_cleanup_action()));
  EXPECT_EQ(global, 12);
}

TEST(AtCoroutineExit, TwoCleanupActionsWithContinuation) {
  global = 0;
  sync_wait(with_continuation(test_two_cleanup_actions()));
  EXPECT_EQ(global, 24);
}

TEST(AtCoroutineExit, OneCleanupActionWithStop) {
  global = 0;
  sync_wait(test_one_cleanup_action_with_stop());
  EXPECT_EQ(global, 2);
}

TEST(AtCoroutineExit, TwoCleanupActionsWithStop) {
  global = 0;
  sync_wait(test_two_cleanup_actions_with_stop());
  EXPECT_EQ(global, 4);
}

TEST(AtCoroutineExit, OneCleanupActionWithStopAndContinuation) {
  global = 0;
  sync_wait(with_continuation(test_one_cleanup_action_with_stop()));
  EXPECT_EQ(global, 2);
}

TEST(AtCoroutineExit, TwoCleanupActionsWithStopAndContinuation) {
  global = 0;
  sync_wait(with_continuation(test_two_cleanup_actions_with_stop()));
  EXPECT_EQ(global, 4);
}

#endif // !UNIFEX_NO_COROUTINES
