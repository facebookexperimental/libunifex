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
#include <unifex/cleanup_adapt_stream.hpp>

#include <unifex/for_each.hpp>
#include <unifex/on_stream.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>

#include <gtest/gtest.h>

using namespace unifex;

TEST(CompleteAdaptStream, Smoke) {
  timed_single_thread_context thread;

  bool completed = false;

  auto stream = cleanup_adapt_stream(
                    on_stream(current_scheduler, range_stream{0, 20}),
                    [&](auto&& cleanup) noexcept {
                      completed = true;
                      return std::forward<decltype(cleanup)>(cleanup);
                    }) |
      for_each([&](auto&&) { EXPECT_FALSE(completed); });

  sync_wait(on(thread.get_scheduler(), std::move(stream)));

  EXPECT_TRUE(completed);
}
