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

using namespace unifex;
using namespace std::chrono;

int main() {
  thread_unsafe_event_loop eventLoop;

  std::printf("starting\n");

  auto startTime = steady_clock::now();

  [[maybe_unused]] std::optional<unit> result =
      eventLoop.sync_wait(for_each(
          take_until(
              stop_immediately<int>(
                  delay(range_stream{0, 100}, eventLoop.get_scheduler(), 50ms)),
              single(schedule_after(eventLoop.get_scheduler(), 500ms))),
          [startTime](int value) {
            auto ms = duration_cast<milliseconds>(steady_clock::now() - startTime);
            std::printf("[%i ms] %i\n", (int)ms.count(), value);
          }));

  return 0;
}
