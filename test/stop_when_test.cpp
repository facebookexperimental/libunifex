/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2 with LLVM exceptions
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

#include <unifex/stop_when.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/on.hpp>

#include <chrono>
#include <optional>

#include <gtest/gtest.h>

TEST(StopWhen, SourceCompletesFirst) {
    using namespace std::chrono_literals;

    unifex::timed_single_thread_context ctx;

    bool sourceExecuted = false;
    bool triggerExecuted = false;
    
    std::optional<int> result = unifex::sync_wait(
        unifex::on(
            ctx.get_scheduler(),
            unifex::stop_when(
                unifex::transform(
                    unifex::schedule_after(10ms),
                    [&] {
                        sourceExecuted = true;
                        return 42;
                    }),
                unifex::transform(
                    unifex::schedule_after(1s),
                    [&] { triggerExecuted = true; }))));

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(42, result.value());

    EXPECT_TRUE(sourceExecuted);
    EXPECT_FALSE(triggerExecuted);
}

TEST(StopWhen, TriggerCompletesFirst) {
    using namespace std::chrono_literals;

    unifex::timed_single_thread_context ctx;

    bool sourceExecuted = false;
    bool triggerExecuted = false;
    
    std::optional<int> result = unifex::sync_wait(
        unifex::on(
            ctx.get_scheduler(),
            unifex::stop_when(
                unifex::transform(
                    unifex::schedule_after(1s),
                    [&] {
                        sourceExecuted = true;
                        return 42;
                    }),
                unifex::transform(
                    unifex::schedule_after(10ms),
                    [&] { triggerExecuted = true; }))));

    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(sourceExecuted);
    EXPECT_TRUE(triggerExecuted);
}

TEST(StopWhen, CancelledFromParent) {
    using namespace std::chrono_literals;

    unifex::timed_single_thread_context ctx;

    bool sourceExecuted = false;
    bool triggerExecuted = false;
    
    std::optional<int> result = unifex::sync_wait(
        unifex::on(
            ctx.get_scheduler(),
            unifex::stop_when(
                unifex::stop_when(
                    unifex::transform(
                        unifex::schedule_after(1s),
                        [&] {
                            sourceExecuted = true;
                            return 42;
                        }),
                    unifex::transform(
                        unifex::schedule_after(2s),
                        [&] {
                            triggerExecuted = true;
                        })),
                unifex::schedule_after(10ms))));

    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(sourceExecuted);
    EXPECT_FALSE(triggerExecuted);
}

TEST(StopWhen, Pipeable) {
    using namespace std::chrono_literals;

    unifex::timed_single_thread_context ctx;

    bool sourceExecuted = false;
    bool triggerExecuted = false;
    
    auto op = unifex::schedule_after(1s)
      | unifex::transform(
        [&] {
            sourceExecuted = true;
            return 42;
        })
      | unifex::stop_when(
          unifex::schedule_after(10ms)
            | unifex::transform(
              [&] { triggerExecuted = true; }));
    std::optional<int> result =
        unifex::sync_wait(unifex::on(ctx.get_scheduler(), std::move(op)));

    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(sourceExecuted);
    EXPECT_TRUE(triggerExecuted);
}
