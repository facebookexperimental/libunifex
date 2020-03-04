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

#include <unifex/retry_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/let.hpp>

#include <exception>
#include <cstdio>
#include <chrono>

using namespace std::chrono_literals;

class some_error : public std::exception {
    const char* what() const noexcept override {
        return "some error";
    }
};

int main() {
  unifex::timed_single_thread_context ctx;
  auto scheduler = ctx.get_scheduler();

  auto start = std::chrono::steady_clock::now();

  auto timeSinceStartInMs = [start] {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start).count();
  };

  int operationCount = 0;

  try {
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
        }));

    // Should have thrown an exception.
    std::printf("error: operation should have failed with an exception\n");
    return 1;
  } catch (const some_error&) {
    std::printf("[%d] caught some_error in main()\n", (int)timeSinceStartInMs());
  }

  const int expectedDurationInMs = 
    10 +
    (100 + 10) +
    (200 + 10) +
    (300 + 10) +
    (400 + 10) + 
    (500 + 10);

  const auto elapsedDurationInMs = timeSinceStartInMs();
  if (elapsedDurationInMs < expectedDurationInMs) {
      std::printf("error: operation completed sooner than expected\n");
      return 2;
  }

  if (operationCount != 6) {
      std::printf("error: operation should have executed 6 times\n");
      return 3;
  }

  return 0;
}
