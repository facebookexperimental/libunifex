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

#include <unifex/just.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/then.hpp>

#include <gtest/gtest.h>

using namespace unifex;

int global;

task<int&> await_reference_awaitable() {
  struct AwaitableGlobalRef {
    bool await_ready() noexcept { return true; }
    void await_suspend(coro::coroutine_handle<>) noexcept {};
    int& await_resume() noexcept { return global; }
  };
  int& x = co_await AwaitableGlobalRef{};
  co_return x;
}

task<int&> await_reference_sender() {
  int& x = co_await then(just(), []() -> int& { return global; });
  co_return x;
}

TEST(Task, AwaitAwaitableReturningReference) {
  global = 0;
  int& ref = sync_wait(await_reference_awaitable()).value();
  EXPECT_EQ(ref, 0);
  global = 10;
  EXPECT_EQ(ref, 10);
}

TEST(Task, AwaitSenderReturningReference) {
  global = 0;
  int& ref = sync_wait(await_reference_sender()).value();
  EXPECT_EQ(ref, 0);
  global = 10;
  EXPECT_EQ(ref, 10);
}

#endif // !UNIFEX_NO_COROUTINES
