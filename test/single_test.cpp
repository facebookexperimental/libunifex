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
#include <unifex/delay.hpp>
#include <unifex/for_each.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/single.hpp>
#include <unifex/stop_immediately.hpp>
#include <unifex/take_until.hpp>
#include <unifex/thread_unsafe_event_loop.hpp>
#include <unifex/typed_via_stream.hpp>

#include <chrono>
#include <cstdio>
#include <optional>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;

TEST(single, Smoke) {
  thread_unsafe_event_loop eventLoop;

  std::printf("starting\n");

  auto start = steady_clock::now();

  [[maybe_unused]] std::optional<unit> result =
      eventLoop.sync_wait(for_each(
          take_until(
              stop_immediately<int>(
                  delay(range_stream{0, 100}, eventLoop.get_scheduler(), 50ms)),
              single(schedule_after(eventLoop.get_scheduler(), 500ms))),
          [start](int value) {
            auto ms = duration_cast<milliseconds>(steady_clock::now() - start);
            std::printf("[%i ms] %i\n", (int)ms.count(), value);
          }));
}

TEST(single, Pipeable) {
  thread_unsafe_event_loop eventLoop;

  std::printf("starting\n");

  auto start = steady_clock::now();

  [[maybe_unused]] std::optional<unit> result = 
    eventLoop.sync_wait(
      range_stream{0, 100} 
        | delay(eventLoop.get_scheduler(), 50ms)
        | stop_immediately<int>()
        | take_until(
            schedule_after(eventLoop.get_scheduler(), 500ms) 
              | single())
        | for_each(
            [start](int value) {
              auto ms = duration_cast<milliseconds>(steady_clock::now() - start);
              std::printf("[%i ms] %i\n", (int)ms.count(), value);
            }));
}