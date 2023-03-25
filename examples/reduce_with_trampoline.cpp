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
#include <unifex/range_stream.hpp>
#include <unifex/reduce_stream.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/trampoline_scheduler.hpp>
#include <unifex/then.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/typed_via_stream.hpp>

#include <cstdio>

using namespace unifex;

// This test uses the trampoline_scheduler to avoid stack-overflow due to very
// deep recursion from a reduce over a synchronous stream.

int main() {
  sync_wait(then(
      reduce_stream(
          typed_via_stream(
              trampoline_scheduler{},
              transform_stream(
                  range_stream{0, 100'000},
                  [](unsigned value) { return value * value; })),
          0,
          [](unsigned state, unsigned value) { return state + 10 * value; }),
      [&](unsigned result) { std::printf("result: %i\n", result); }));

  return 0;
}
