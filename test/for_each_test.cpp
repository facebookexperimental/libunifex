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
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/trampoline_scheduler.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/via_stream.hpp>

#include <cstdio>

#include <gtest/gtest.h>

using namespace unifex;

TEST(ForEach, Smoke) {
  sync_wait(then(
      for_each(
          transform_stream(
              range_stream{0, 10}, [](int value) { return value * value; }),
          [](int value) { std::printf("got %i\n", value); }),
      []() { std::printf("done\n"); }));
}

TEST(ForEach, Pipeable) {
  range_stream{0, 10} |
      transform_stream([](int value) { return value * value; }) |
      for_each([](int value) { std::printf("got %i\n", value); }) |
      then([]() { std::printf("done\n"); }) | sync_wait();
}
