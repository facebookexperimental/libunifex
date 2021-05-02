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
#include <unifex/invoke.hpp>
#include <unifex/task.hpp>
#include <unifex/sync_wait.hpp>

#include <optional>

#include <gtest/gtest.h>

using namespace unifex;

TEST(CoInvoke, NoArgumentsNoCaptures) {
  task<int> t = co_invoke([]() -> task<int> { co_return 42; });
  std::optional<int> result = sync_wait(std::move(t));
  ASSERT_TRUE(!!result);
  EXPECT_EQ(*result, 42);
}

TEST(CoInvoke, NoArgumentsWithByValueCaptures) {
  int i = 42;
  task<int> t = co_invoke([=]() -> task<int> { co_return i; });
  std::optional<int> result = sync_wait(std::move(t));
  ASSERT_TRUE(!!result);
  EXPECT_EQ(*result, 42);
}

TEST(CoInvoke, NoArgumentsWithByRefCaptures) {
  int i = 42;
  task<int> t = co_invoke([&]() -> task<int> { co_return i; });
  std::optional<int> result = sync_wait(std::move(t));
  ASSERT_TRUE(!!result);
  EXPECT_EQ(*result, 42);
}

TEST(CoInvoke, WithArgumentsWithByValueCaptures) {
  int i = 42;
  task<int> t = co_invoke([=](int j) -> task<int> { co_return i + j; }, 58);
  std::optional<int> result = sync_wait(std::move(t));
  ASSERT_TRUE(!!result);
  EXPECT_EQ(*result, 100);
}

TEST(CoInvoke, WithArgumentsWithByRefCaptures) {
  int i = 42;
  task<int> t = co_invoke([&](int j) -> task<int> { co_return i + j; }, 58);
  std::optional<int> result = sync_wait(std::move(t));
  ASSERT_TRUE(!!result);
  EXPECT_EQ(*result, 100);
}
