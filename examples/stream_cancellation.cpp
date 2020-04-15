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
#include <unifex/for_each.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/on.hpp>
#include <unifex/on_stream.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/trampoline_scheduler.hpp>
#include <unifex/stop_when.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace unifex;
using namespace std::literals::chrono_literals;

int main() {
  timed_single_thread_context context;

  using namespace std::chrono;

  auto start = steady_clock::now();

  sync_wait(
      on(
        stop_when(
          for_each(
             on_stream(trampoline_scheduler{}, range_stream{0, 20}),
             [](int value) {
               // Simulate some work
               std::printf("processing %i\n", value);
               std::this_thread::sleep_for(10ms);
             }),
          schedule_after(100ms)),
        context.get_scheduler()));

  auto end = steady_clock::now();

  std::printf(
      "took %i ms\n", (int)duration_cast<milliseconds>(end - start).count());

  return 0;
}
