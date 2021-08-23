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

#ifdef _WIN32

#include <unifex/win32/windows_thread_pool.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/let_done.hpp>
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

    auto incrementCountOnTp = unifex::then(unifex::schedule(s), [&] { ++count; });

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
        unifex::then(
            unifex::schedule(tp.get_scheduler()),
            [&]() noexcept { return std::this_thread::get_id(); }));
    EXPECT_NE(workThreadId, mainThreadId);
}

TEST(windows_thread_pool, schedule_multiple_in_parallel) {
    unifex::win32::windows_thread_pool tp;
    auto sch = tp.get_scheduler();

    unifex::sync_wait(unifex::then(
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
        unifex::let_done(
            unifex::stop_when(
                unifex::repeat_effect(unifex::schedule(sch)),
                unifex::schedule(sch)),
            [] { return unifex::just(); }),
        [n=0]() mutable noexcept { return n++ == 1000; }));
}

TEST(windows_thread_pool, schedule_after) {
    unifex::win32::windows_thread_pool tp;
    auto s = tp.get_scheduler();

    auto startTime = s.now();

    unifex::sync_wait(unifex::schedule_after(s, 50ms));

    auto duration = s.now() - startTime;

    EXPECT_TRUE(duration > 40ms);
    EXPECT_TRUE(duration < 100ms);
}

TEST(windows_thread_pool, schedule_after_cancellation) {
    unifex::win32::windows_thread_pool tp;
    auto s = tp.get_scheduler();

    auto startTime = s.now();

    bool ranWork = false;

    unifex::sync_wait(
        unifex::let_done(
            unifex::stop_when(
                unifex::then(
                    unifex::schedule_after(s, 5s),
                    [&] { ranWork = true; }),
                unifex::schedule_after(s, 5ms)),
            [] { return unifex::just(); }));

    auto duration = s.now() - startTime;

    // Work should have been cancelled.
    EXPECT_FALSE(ranWork);
    EXPECT_LT(duration, 1s);
}

TEST(windows_thread_pool, schedule_at) {
    unifex::win32::windows_thread_pool tp;
    auto s = tp.get_scheduler();

    auto startTime = s.now();

    unifex::sync_wait(unifex::schedule_at(s, startTime + 100ms));

    auto endTime = s.now();
    EXPECT_TRUE(endTime >= (startTime + 100ms));
    EXPECT_TRUE(endTime < (startTime + 150ms));
}

TEST(windows_thread_pool, schedule_at_cancellation) {
    unifex::win32::windows_thread_pool tp;
    auto s = tp.get_scheduler();

    auto startTime = s.now();

    bool ranWork = false;

    unifex::sync_wait(
        unifex::let_done(
            unifex::stop_when(
                unifex::then(
                    unifex::schedule_at(s, startTime + 5s),
                    [&] { ranWork = true; }),
                unifex::schedule_at(s, startTime + 5ms)),
            [] { return unifex::just(); }));

    auto duration = s.now() - startTime;

    // Work should have been cancelled.
    EXPECT_FALSE(ranWork);
    EXPECT_LT(duration, 1s);
}

#endif // _WIN32
