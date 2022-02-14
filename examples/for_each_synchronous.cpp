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
#include <unifex/sync_wait.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/for_each.hpp>
#include <unifex/then.hpp>
#include <unifex/range_stream.hpp>

#include <cstdio>

using namespace unifex;

int main() {
  sync_wait(then(
      for_each(
          transform_stream(
              range_stream{0, 10}, [](int value) { return value * value; }),
          [](int value) { std::printf("got %i\n", value); }),
      []() { std::printf("done\n"); }));

  return 0;
}
