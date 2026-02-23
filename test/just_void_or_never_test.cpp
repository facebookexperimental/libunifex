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
#include <unifex/just_void_or_never.hpp>

#include <optional>

#include <unifex/get_stop_token.hpp>
#include <unifex/just_from.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/with_query_value.hpp>

#include <gtest/gtest.h>

namespace unifex {
namespace {

TEST(just_void_or_never, true_completes_with_value) {
  std::optional<int> i =
      sync_wait(let_value_with_stop_source([](auto&) noexcept {
        return then(just_void_or_never(true), [] { return 42; });
      }));
  ASSERT_TRUE(i.has_value());
  EXPECT_EQ(*i, 42);
}

TEST(just_void_or_never, false_completes_via_stop) {
  std::optional<int> i =
      sync_wait(let_value_with_stop_source([](auto& stopSource) noexcept {
        stopSource.request_stop();
        return then(just_void_or_never(false), [] { return 42; });
      }));
  ASSERT_FALSE(i.has_value());
}

TEST(just_void_or_never, true_with_unstoppable_token) {
  std::optional<int> i = sync_wait(then(
      with_query_value(
          just_void_or_never(true), get_stop_token, unstoppable_token{}),
      [] { return 42; }));
  ASSERT_TRUE(i.has_value());
  EXPECT_EQ(*i, 42);
}

TEST(just_void_or_never, false_cancelled_after_start) {
  auto i = sync_wait(stop_when(just_void_or_never(false), just()));
  ASSERT_FALSE(i.has_value());
}

}  // namespace
}  // namespace unifex
