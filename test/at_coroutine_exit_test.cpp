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

#include <unifex/at_coroutine_exit.hpp>

#include <unifex/task.hpp>
#include <unifex/stop_if_requested.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/just_from.hpp>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
struct AtCoroutineExit : testing::Test {
  int result = 0;

  task<void> test_one_cleanup_action() {
    ++result;
    co_await at_coroutine_exit([this]() -> task<void> { result *= 2; co_return; });
    ++result;
  }

  task<void> test_two_cleanup_actions() {
    ++result;
    co_await at_coroutine_exit([this]() -> task<void> { result *= 2; co_return; });
    co_await at_coroutine_exit([this]() -> task<void> { result *= result; co_return; });
    ++result;
  }

  task<void> test_one_cleanup_action_with_stop() {
    ++result;
    co_await at_coroutine_exit([this]() -> task<void> { result *= 2; co_return; });
    co_await stop();
    ++result;
  }

  task<void> test_two_cleanup_actions_with_stop() {
    ++result;
    co_await at_coroutine_exit([this]() -> task<void> { result *= 2; co_return; });
    co_await at_coroutine_exit([this]() -> task<void> { result *= result; co_return; });
    co_await stop();
    ++result;
  }

  task<void> test_sender_cleanup_action() {
    co_await at_coroutine_exit([this]{ return just_from([this]{++result;}); });
  }

  task<void> test_stateful_cleanup_action(int arg) {
    co_await at_coroutine_exit([arg,this]{ return just_from([arg,this]{result += arg;}); });
  }

  task<void> test_mutable_stateful_cleanup_action() {
    auto&& [i] = co_await at_coroutine_exit(
      [this](int&& i) -> task<void> {
        result += i;
        co_return;
      }, 3);
    ++result;
    i *= i;
  }

  task<void> with_continuation(unifex::task<void> next) {
    co_await std::move(next);
    result *= 3;
  }

  void test_cancel_in_cleanup_action_causes_death() {
    task<void> t = []() -> task<void> {
      co_await at_coroutine_exit([]() -> task<void> {
        co_await stop();
      });
    }();
    sync_wait(std::move(t)); // causes termination
    ADD_FAILURE() << "He didn't fall? Inconceivable!";
  }

  void test_cancel_during_cancellation_unwind_causes_death() {
    task<void> t = []() -> task<void> {
      co_await at_coroutine_exit([]() -> task<void> {
        co_await stop(); // BOOM
      });
      co_await stop();
    }();
    sync_wait(std::move(t)); // causes termination
    ADD_FAILURE() << "He didn't fall? Inconceivable!";
  }

  void test_throw_in_cleanup_action_causes_death() {
    task<void> t = []() -> task<void> {
      co_await at_coroutine_exit([]() -> task<void> {
        throw 42;
      });
    }();
    sync_wait(std::move(t)); // causes termination
    ADD_FAILURE() << "He didn't fall? Inconceivable!";
  }

  void test_throw_in_cleanup_action_during_exception_unwind_causes_death() {
    task<void> t = []() -> task<void> {
      co_await at_coroutine_exit([]() -> task<void> {
        throw 42;
      });
      throw 42;
    }();
    sync_wait(std::move(t)); // causes termination
    ADD_FAILURE() << "He didn't fall? Inconceivable!";
  }

  void test_cancel_in_cleanup_action_during_exception_unwind_causes_death() {
    task<void> t = []() -> task<void> {
      co_await at_coroutine_exit([]() -> task<void> {
        co_await stop();
      });
      throw 42;
    }();
    sync_wait(std::move(t)); // causes termination
    ADD_FAILURE() << "He didn't fall? Inconceivable!";
  }

  void test_throw_in_cleanup_action_during_cancellation_unwind_causes_death() {
    task<void> t = []() -> task<void> {
      co_await at_coroutine_exit([]() -> task<void> {
        throw 42;
      });
      co_await stop();
    }();
    sync_wait(std::move(t)); // causes termination
    ADD_FAILURE() << "He didn't fall? Inconceivable!";
  }
};
} // unnamed namespace

TEST_F(AtCoroutineExit, OneCleanupAction) {
  sync_wait(test_one_cleanup_action());
  EXPECT_EQ(result, 4);
}

TEST_F(AtCoroutineExit, TwoCleanupActions) {
  sync_wait(test_two_cleanup_actions());
  EXPECT_EQ(result, 8);
}

TEST_F(AtCoroutineExit, OneCleanupActionWithContinuation) {
  sync_wait(with_continuation(test_one_cleanup_action()));
  EXPECT_EQ(result, 12);
}

TEST_F(AtCoroutineExit, TwoCleanupActionsWithContinuation) {
  sync_wait(with_continuation(test_two_cleanup_actions()));
  EXPECT_EQ(result, 24);
}

TEST_F(AtCoroutineExit, OneCleanupActionWithStop) {
  sync_wait(test_one_cleanup_action_with_stop());
  EXPECT_EQ(result, 2);
}

TEST_F(AtCoroutineExit, TwoCleanupActionsWithStop) {
  sync_wait(test_two_cleanup_actions_with_stop());
  EXPECT_EQ(result, 2);
}

TEST_F(AtCoroutineExit, OneCleanupActionWithStopAndContinuation) {
  sync_wait(with_continuation(test_one_cleanup_action_with_stop()));
  EXPECT_EQ(result, 2);
}

TEST_F(AtCoroutineExit, TwoCleanupActionsWithStopAndContinuation) {
  sync_wait(with_continuation(test_two_cleanup_actions_with_stop()));
  EXPECT_EQ(result, 2);
}

TEST_F(AtCoroutineExit, StatefulCleanupAction) {
  sync_wait(test_stateful_cleanup_action(42));
  EXPECT_EQ(result, 42);
}

TEST_F(AtCoroutineExit, MutableStatefulCleanupAction) {
  sync_wait(test_mutable_stateful_cleanup_action());
  EXPECT_EQ(result, 10);
}

TEST_F(AtCoroutineExit, CancelInCleanupActionCallsTerminate) {
  ASSERT_DEATH_IF_SUPPORTED(
    test_cancel_in_cleanup_action_causes_death(),
    "");
}

TEST_F(AtCoroutineExit, CancelDuringCancellationUnwindCallsTerminate) {
  ASSERT_DEATH_IF_SUPPORTED(
    test_cancel_during_cancellation_unwind_causes_death(),
    "");
}

TEST_F(AtCoroutineExit, ThrowInCleanupActionCallsTerminate) {
  ASSERT_DEATH_IF_SUPPORTED(
    test_throw_in_cleanup_action_causes_death(),
    "");
}

TEST_F(AtCoroutineExit, ThrowInCleanupActionDuringExceptionUnwindCallsTerminate) {
  ASSERT_DEATH_IF_SUPPORTED(
    test_throw_in_cleanup_action_during_exception_unwind_causes_death(),
    "");
}

TEST_F(AtCoroutineExit, CancelInCleanupActionDuringExceptionUnwindCallsTerminate) {
  ASSERT_DEATH_IF_SUPPORTED(
    test_cancel_in_cleanup_action_during_exception_unwind_causes_death(),
    "");
}

TEST_F(AtCoroutineExit, ThrowInCleanupActionDuringCancellationUnwindCallsTerminate) {
  ASSERT_DEATH_IF_SUPPORTED(
    test_throw_in_cleanup_action_during_cancellation_unwind_causes_death(),
    "");
}

#endif // !UNIFEX_NO_COROUTINES
