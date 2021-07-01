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
#include <unifex/just.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/on.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/unstoppable.hpp>

#include <gtest/gtest.h>

using namespace unifex;

TEST(Unstoppable, Smoke) {
  single_thread_context thread;

  auto result =
      sync_wait(let_value_with_stop_source([&](auto& stopSource) noexcept {
        stopSource.request_stop();

        return unstoppable(on(thread.get_scheduler(), just()));
      }));
  EXPECT_TRUE(result.has_value());
}
