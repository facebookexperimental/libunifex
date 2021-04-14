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

#ifdef _WIN32

#include <unifex/win32/windows_thread_pool.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/when_all.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/transform_done.hpp>
#include <unifex/just.hpp>
#include <unifex/materialize.hpp>

#include <chrono>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(windows_thread_pool, construct_destruct) {
    unifex::win32::windows_thread_pool tp;
}

TEST(windows_thread_pool, custom_thread_pool) {
    unifex::win32::windows_thread_pool tp{2, 4};
    auto s = tp.get_scheduler();

    std::atomic<int> count = 0;

    auto incrementCountOnTp = unifex::transform(unifex::schedule(s), [&] { ++count; });

    unifex::sync_wait(unifex::when_all(
        incrementCountOnTp,
        incrementCountOnTp,
        incrementCountOnTp,
        incrementCountOnTp));

    EXPECT_EQ(4, count.load());
}

TEST(windows_thread_pool, schedule) {
    unifex::win32::windows_thread_pool tp;
    unifex::sync_wait(unifex::schedule(tp.get_scheduler()));
}

TEST(windows_thread_pool, schedule_completes_on_a_different_thread) {
    unifex::win32::windows_thread_pool tp;
    const auto mainThreadId = std::this_thread::get_id();
    auto workThreadId = unifex::sync_wait(
        unifex::transform(
            unifex::schedule(tp.get_scheduler()),
            [&]() noexcept { return std::this_thread::get_id(); }));
    EXPECT_NE(workThreadId, mainThreadId);
}

TEST(windows_thread_pool, schedule_multiple_in_parallel) {
    unifex::win32::windows_thread_pool tp;
    auto sch = tp.get_scheduler();

    unifex::sync_wait(unifex::transform(
        unifex::when_all(
            unifex::schedule(sch),
            unifex::schedule(sch),
            unifex::schedule(sch)),
        [](auto&&...) noexcept { return 0; }));
}

TEST(windows_thread_pool, schedule_cancellation_thread_safety) {
    unifex::win32::windows_thread_pool tp;
    auto sch = tp.get_scheduler();

    unifex::sync_wait(unifex::repeat_effect_until(
        unifex::transform_done(
            unifex::stop_when(
                unifex::repeat_effect(unifex::schedule(sch)),
                unifex::schedule(sch)),
            [] { return unifex::just(); }),
        [n=0]() mutable noexcept { return n++ == 1000; }));
}

TEST(windows_thread_pool, schedule_after) {
    unifex::win32::windows_thread_pool tp;
    auto s = tp.get_scheduler();

    auto start = s.now();

    unifex::sync_wait(unifex::schedule_after(s, 50ms));

    auto duration = s.now() - start;

    EXPECT_TRUE(duration > 40ms);
    EXPECT_TRUE(duration < 100ms);
}

TEST(windows_thread_pool, schedule_after_cancellation) {
    unifex::win32::windows_thread_pool tp;
    auto s = tp.get_scheduler();

    auto start = s.now();

    bool ranWork = false;

    unifex::sync_wait(
        unifex::transform_done(
            unifex::stop_when(
                unifex::transform(
                    unifex::schedule_after(s, 5s),
                    [&] { ranWork = true; }),
                unifex::schedule_after(s, 5ms)),
            [] { return unifex::just(); }));

    auto duration = s.now() - start;

    // Work should have been cancelled.
    EXPECT_FALSE(ranWork);
    EXPECT_LT(duration, 1s);
}

TEST(windows_thread_pool, schedule_at) {
    unifex::win32::windows_thread_pool tp;
    auto s = tp.get_scheduler();

    auto start = s.now();

    unifex::sync_wait(unifex::schedule_at(s, start + 100ms));

    auto end = s.now();   
    EXPECT_TRUE(end >= (start + 100ms));
    EXPECT_TRUE(end < (start + 150ms));
}

TEST(windows_thread_pool, schedule_at_cancellation) {
    unifex::win32::windows_thread_pool tp;
    auto s = tp.get_scheduler();

    auto start = s.now();

    bool ranWork = false;

    unifex::sync_wait(
        unifex::transform_done(
            unifex::stop_when(
                unifex::transform(
                    unifex::schedule_at(s, start + 5s),
                    [&] { ranWork = true; }),
                unifex::schedule_at(s, start + 5ms)),
            [] { return unifex::just(); }));

    auto duration = s.now() - start;

    // Work should have been cancelled.
    EXPECT_FALSE(ranWork);
    EXPECT_LT(duration, 1s);
}

#endif // _WIN32
