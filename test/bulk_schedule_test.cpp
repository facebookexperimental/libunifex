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

#include <unifex/bulk_schedule.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/bulk_transform.hpp>
#include <unifex/bulk_join.hpp>
#include <unifex/let_value_with_stop_source.hpp>

#include <gtest/gtest.h>

TEST(bulk, bulk_transform) {
    unifex::single_thread_context ctx;
    auto sched = ctx.get_scheduler();

    const std::size_t count = 1000;

    std::vector<int> output;
    output.resize(count);

    unifex::sync_wait(
        unifex::bulk_join(
            unifex::bulk_transform(
                unifex::bulk_transform(
                    unifex::bulk_schedule(sched, count),
                    [](std::size_t index) noexcept {
                        // Reverse indices
                        return count - 1 - index;
                    }, unifex::par_unseq),
                [&](std::size_t index) noexcept {
                    output[index] = static_cast<int>(index);
                }, unifex::par_unseq)));

    for (std::size_t i = 0; i < count; ++i) {
        EXPECT_EQ(i, output[i]);
    }
}

TEST(bulk, cancellation) {
    unifex::single_thread_context ctx;
    auto sched = ctx.get_scheduler();

    const std::size_t count = 1000;

    std::vector<int> output(count, 0);
    // Cancel after two chunks
    // For the serial implementation this will stop the third chunk onwards from
    // being dispatched.
    const std::size_t compare_index = unifex::bulk_cancellation_chunk_size*2 - 1;

    // Bulk, but sequential to test strict cancellation of later work

    unifex::sync_wait(
        unifex::let_value_with_stop_source([&](unifex::inplace_stop_source& stopSource) {
            return unifex::bulk_join(
                unifex::bulk_transform(
                    unifex::bulk_schedule(sched, count),
                    [&](std::size_t index) noexcept {
                        // Stop after second chunk
                        if(index == compare_index) {
                            stopSource.request_stop();
                        }
                        output[index] = static_cast<int>(index);
                    }, unifex::seq));
        }));

    for (std::size_t i = 0; i <= compare_index; ++i) {
        EXPECT_EQ(static_cast<int>(i), output[i]);
    }
    for (std::size_t i = compare_index+1; i < count; ++i) {
        EXPECT_EQ(0, output[i]);
    }
}

TEST(bulk, Pipeable) {
    unifex::single_thread_context ctx;
    auto sched = ctx.get_scheduler();

    const std::size_t count = 1000;

    std::vector<int> output;
    output.resize(count);

    unifex::bulk_schedule(sched, count)
      | unifex::bulk_transform(
        [](std::size_t index) noexcept {
            // Reverse indices
            return count - 1 - index;
        }, unifex::par_unseq)
      | unifex::bulk_transform(
        [&](std::size_t index) noexcept {
            output[index] = static_cast<int>(index);
        }, unifex::par_unseq)
      | unifex::bulk_join()
      | unifex::sync_wait();

    for (std::size_t i = 0; i < count; ++i) {
        EXPECT_EQ(i, output[i]);
    }
}
