/*
 * Copyright 2020-present Facebook, Inc.
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

#include <cassert>

#include <gtest/gtest.h>

TEST(windows_thread_pool, construct_destruct) {
    unifex::win32::windows_thread_pool tp;
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

#endif
