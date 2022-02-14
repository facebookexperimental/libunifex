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
#include <unifex/range_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/via_stream.hpp>
#include <unifex/with_query_value.hpp>

#include <chrono>

using namespace unifex;
using namespace std::chrono_literals;

int main() {
  timed_single_thread_context ctx;

  // Check that the schedule() operation can pick up the current
  // scheduler from the receiver which we inject by using 'with_query_value()'.
  sync_wait(with_query_value(schedule(), get_scheduler,
                             ctx.get_scheduler()));

  // Check that the schedule_after(d) operation can pick up the current
  // scheduler from the receiver.
  sync_wait(with_query_value(
      schedule_after(200ms), get_scheduler, ctx.get_scheduler()));

  // Check that this can propagate through multiple levels of
  // composed operations.
  sync_wait(with_query_value(
      then(
          for_each(via_stream(current_scheduler,
                              transform_stream(range_stream{0, 10},
                                               [](int value) {
                                                 return value * value;
                                               })),
                   [](int value) { std::printf("got %i\n", value); }),
          []() { std::printf("done\n"); }),
      get_scheduler, ctx.get_scheduler()));

  return 0;
}
