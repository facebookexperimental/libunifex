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
#include <unifex/config.hpp>

#if !UNIFEX_NO_COROUTINES

#include <unifex/awaitable_sender.hpp>
#include <unifex/delay.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_awaitable.hpp>
#include <unifex/single.hpp>
#include <unifex/stop_immediately.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/take_until.hpp>
#include <unifex/task.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/typed_via_stream.hpp>

#include <chrono>
#include <cstdio>

using namespace unifex;

int main() {
  using namespace std::chrono;

  timed_single_thread_context context;

  auto makeTask = [&]() -> task<int> {
    auto start = steady_clock::now();

    auto s = take_until(
        stop_immediately<int>(
            delay(range_stream{0, 100}, context.get_scheduler(), 50ms)),
        single(schedule_after(context.get_scheduler(), 500ms)));

    int sum = 0;
    while (auto value = co_await next(s)) {
      auto ms = duration_cast<milliseconds>(steady_clock::now() - start);
      std::printf("[%i ms] %i\n", (int)ms.count(), *value);
      std::fflush(stdout);

      sum += *value;
    }

    co_await cleanup(s);

    auto ms = duration_cast<milliseconds>(steady_clock::now() - start);
    std::printf("[%i ms] sum = %i\n", (int)ms.count(), sum);
    std::fflush(stdout);

    co_return sum;
  };

  sync_wait(awaitable_sender{makeTask()});

  return 0;
}

#else // UNIFEX_NO_COROUTINES

#include <cstdio>

int main() {
  std::printf(
      "This test only supported for compilers that support coroutines\n");
  return 0;
}

#endif // UNIFEX_NO_COROUTINES
