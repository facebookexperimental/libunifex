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
#include <unifex/just.hpp>
#include <unifex/let.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/static_thread_pool.hpp>
#include <unifex/find_if.hpp>
#include <unifex/transform.hpp>
#include <unifex/when_all.hpp>
#include <unifex/on.hpp>

#include <gtest/gtest.h>

TEST(find_if, find_if_sequential) {
    using namespace unifex;

    std::vector<int> input{1, 2, 3, 4};
    // Apply linear find_if.
    // As for std::find_if it returns the first instance that matches the
    // predicate where the algorithm takes an iterator pair as the first
    // two arguments, and forwards all other arguments to the predicate and
    // onwards to the result.
    // Precise API shape for the data being passed through is TBD, this is
    // one option only.
    std::optional<std::vector<int>::iterator> result = sync_wait(transform(
        find_if(
            just(begin(input), end(input), 3),
            [&](const int& v, int another_parameter) noexcept {
              return v == another_parameter;
            },
            unifex::seq),
        [](std::vector<int>::iterator v, int another_parameter) noexcept {
          assert(another_parameter == 3);
          return v;
        }));

    EXPECT_EQ(**result, 3);
}


TEST(find_if, find_if_parallel) {
    using namespace unifex;

    std::vector<int> input;
    std::atomic<int> countOfTasksRun = 0;
    constexpr int checkValue = 7;

    for(int i = 2; i < 128; ++i) {
      input.push_back(i);
    }
    static_thread_pool ctx;
    std::optional<std::vector<int>::iterator> result = sync_wait(
      unifex::on(
        transform(
          find_if(
              just(begin(input), end(input), checkValue),
              [&](const int& v, int another_parameter) noexcept {
                // Count to make sure that cancellation is triggered
                countOfTasksRun++;
                return v == another_parameter;
              },
              unifex::par),
          [](std::vector<int>::iterator v, int another_parameter) noexcept {
            assert(another_parameter == checkValue);
            return v;
          }),
        ctx.get_scheduler()));

    EXPECT_EQ(**result, checkValue);
    // Expect 62 iterations to run to validate cancellation
    // This is based on some implementation details:
    //  * bulk_schedule's bulk_cancellation_chunk_size
    //  * find_if's max_num_chunks and min_chunk_size
    // find_if tries to launch 32 chunks of 4 elements each.
    // bulk_schedule then chunks those into 16 element cancellation chunks.
    // bulk_schedule launches a whole 16 element chunk.
    // Most of them run to completion without finding the element.
    // The second finds element 7 half way through and terminates early in
    // the find_if algorithm.
    // It ends up running the comparison operator 62 times.
    // In general cancellation is best effort.
    // Note though that the current implementation launches tasks in-order, so it
    // cannot cancel earlier tasks, which makes find_if's find-fist rule safe.
    EXPECT_EQ(countOfTasksRun, 62);
}

TEST(find_if, Pipeable) {
    using namespace unifex;

    static_thread_pool ctx;

    std::vector<int> input{1, 2, 3, 4};
    // Apply linear find_if.
    // As for std::find_if it returns the first instance that matches the
    // predicate where the algorithm takes an iterator pair as the first
    // two arguments, and forwards all other arguments to the predicate and
    // onwards to the result.
    // Precise API shape for the data being passed through is TBD, this is
    // one option only.
    std::optional<std::vector<int>::iterator> result = just(begin(input), end(input), 3)
      | find_if(
          [&](const int& v, int another_parameter) noexcept {
            return v == another_parameter;
          },
          unifex::seq)
      | transform(
          [](std::vector<int>::iterator v, int another_parameter) noexcept {
            assert(another_parameter == 3);
            return v;
          })
      | on(ctx.get_scheduler())
      | sync_wait();

    EXPECT_EQ(**result, 3);
}
