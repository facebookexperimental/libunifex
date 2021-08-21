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

#include <unifex/for_each.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/never.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <chrono>
#include <cstdio>
#include <optional>
#include <thread>

using namespace std::literals::chrono_literals;
using namespace unifex;

int main() {
  timed_single_thread_context context;

  std::optional<unit> result = sync_wait(
    stop_when(
      for_each(
          never_stream{},
          [](auto) {
            std::printf("got value");
            std::fflush(stdout);
          }),
      then(
        schedule_after(context.get_scheduler(), 100ms),
        [] { std::printf("trigger completing, about to request stop\n"); })));

  std::printf("completed with %s\n", result ? "unit" : "nullopt");

  return 0;
}
