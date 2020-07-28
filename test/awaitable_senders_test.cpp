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

#  include <unifex/awaitable_sender.hpp>
#  include <unifex/just.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>

#  include <gtest/gtest.h>

using namespace unifex;

TEST(awaitable_senders, non_void) {
  auto makeTask = [&]() -> task<std::optional<int>> {
    co_return co_await just(42);
  };

  std::optional<std::optional<int>> answer =
      sync_wait(awaitable_sender(makeTask()));

  EXPECT_TRUE(answer.has_value() && answer->has_value());
  EXPECT_EQ(42, **answer);
}

TEST(awaitable_senders, void) {
  auto makeTask = [&]() -> task<std::optional<unifex::unit>> {
    co_return co_await just();
  };

  std::optional<std::optional<unifex::unit>> answer =
      sync_wait(awaitable_sender(makeTask()));

  EXPECT_TRUE(answer.has_value() && answer->has_value());
}

#endif  // UNIFEX_NO_COROUTINES
