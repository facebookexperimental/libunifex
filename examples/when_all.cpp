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
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/transform.hpp>
#include <unifex/when_all.hpp>

#include <chrono>
#include <iostream>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

int main() {
  timed_single_thread_context context;
  auto scheduler = context.get_scheduler();

  auto startTime = steady_clock::now();

  sync_wait(transform(
      when_all(
          transform(
              schedule_after(scheduler, 100ms),
              [=]() {
                auto time = steady_clock::now() - startTime;
                auto timeMs = duration_cast<milliseconds>(time).count();
                std::cout << "part1 finished - [" << timeMs << "]\n";
                return time;
              }),
          transform(
              schedule_after(scheduler, 200ms),
              [=]() {
                auto time = steady_clock::now() - startTime;
                auto timeMs = duration_cast<milliseconds>(time).count();
                std::cout << "part2 finished - [" << timeMs << "]\n";
                return time;
              })),
      [](auto&& a, auto&& b) {
        std::cout
            << "when_all finished - ["
            << duration_cast<milliseconds>(std::get<0>(var::get<0>(a))).count()
            << ", "
            << duration_cast<milliseconds>(std::get<0>(var::get<0>(b))).count()
            << "]\n";
      }));

  std::cout << "all done\n";

  return 0;
}
