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
#include <unifex/inplace_stop_token.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/typed_via_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/transform.hpp>
#include <unifex/stop_when.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace unifex;

int main() {
  using namespace std::chrono;

  timed_single_thread_context context;

  auto start = steady_clock::now();

  sync_wait(
      stop_when(
          for_each(
              delay(range_stream{0, 100}, context.get_scheduler(), 100ms),
              [start](int value) {
                auto ms = duration_cast<milliseconds>(steady_clock::now() - start);
                std::printf("[%i ms] %i\n", (int)ms.count(), value);
              }),
          transform(
            schedule_after(context.get_scheduler(), 500ms),
            [] { std::printf("cancelling\n"); })));

  return 0;
}
