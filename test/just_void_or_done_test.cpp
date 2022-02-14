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
#include <unifex/just_void_or_done.hpp>

#include <optional>

#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>

#include <gtest/gtest.h>

namespace unifex {
namespace {

TEST(just_void_or_done, just_void) {
  std::optional<int> i =
      sync_wait(then(just_void_or_done(true), [] { return 42; }));
  ASSERT_TRUE(i.has_value());
  EXPECT_EQ(*i, 42);
}

TEST(just_void_or_done, just_done) {
  std::optional<int> i =
      sync_wait(then(just_void_or_done(false), [] { return 42; }));
  ASSERT_FALSE(i.has_value());
}
}  // namespace
}  // namespace unifex
