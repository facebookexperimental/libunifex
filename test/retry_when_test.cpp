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

#include <unifex/config.hpp>

#if !UNIFEX_NO_EXCEPTIONS

#include <unifex/retry_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/let.hpp>

#include <exception>
#include <cstdio>
#include <chrono>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace {
class some_error : public std::exception {
    const char* what() const noexcept override {
        return "some error";
    }
};
} // anonymous namespace

TEST(retry_when, WorksAsExpected) {
  unifex::timed_single_thread_context ctx;
  auto scheduler = ctx.get_scheduler();

  auto startTime = std::chrono::steady_clock::now();

  auto timeSinceStartInMs = [startTime] {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - startTime).count();
  };

  int operationCount = 0;

  EXPECT_THROW(
    unifex::sync_wait(
      unifex::retry_when(
        unifex::transform(unifex::schedule_after(scheduler, 10ms), [&] {
            ++operationCount;
            std::printf("[%d] %d: operation about to fail\n", (int)timeSinceStartInMs(), operationCount);
            throw some_error{};
          }),
        [count = 0, scheduler](std::exception_ptr ex) mutable {
          if (++count > 5) {
            std::printf("retry limit exceeded\n");
            std::rethrow_exception(ex);
          }

          // Simulate some back-off strategy that increases the timeout.
          return unifex::schedule_after(scheduler, count * 100ms);
        })), some_error);

  const int expectedDurationInMs =
    10 +
    (100 + 10) +
    (200 + 10) +
    (300 + 10) +
    (400 + 10) +
    (500 + 10);

  const auto elapsedDurationInMs = timeSinceStartInMs();
  EXPECT_GE(elapsedDurationInMs, expectedDurationInMs)
      << "error: operation completed sooner than expected";

  EXPECT_EQ(operationCount, 6)
      << "error: operation should have executed 6 times";
}

TEST(retry_when, Pipeable) {
  unifex::timed_single_thread_context ctx;
  auto scheduler = ctx.get_scheduler();

  auto startTime = std::chrono::steady_clock::now();

  auto timeSinceStartInMs = [startTime] {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - startTime).count();
  };

  int operationCount = 0;

  EXPECT_THROW(
    unifex::schedule_after(scheduler, 10ms)
      | unifex::transform([&] {
          ++operationCount;
          std::printf("[%d] %d: operation about to fail\n", (int)timeSinceStartInMs(), operationCount);
          throw some_error{};
        })
      | unifex::retry_when(
          [count = 0, scheduler](std::exception_ptr ex) mutable {
            if (++count > 5) {
              std::printf("retry limit exceeded\n");
              std::rethrow_exception(ex);
            }

            // Simulate some back-off strategy that increases the timeout.
            return unifex::schedule_after(scheduler, count * 100ms);
          })
      | unifex::sync_wait(), some_error);

  const int expectedDurationInMs =
    10 +
    (100 + 10) +
    (200 + 10) +
    (300 + 10) +
    (400 + 10) +
    (500 + 10);

  const auto elapsedDurationInMs = timeSinceStartInMs();
  EXPECT_GE(elapsedDurationInMs, expectedDurationInMs)
      << "error: operation completed sooner than expected";

  EXPECT_EQ(operationCount, 6)
      << "error: operation should have executed 6 times";
}

#endif // !UNIFEX_NO_EXCEPTIONS
