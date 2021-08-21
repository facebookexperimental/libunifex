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
#include <unifex/reduce_stream.hpp>
#include <unifex/then.hpp>
#include <unifex/range_stream.hpp>

#include <cstdio>

#include <gtest/gtest.h>

using namespace unifex;

TEST(reduce_stream, Smoke) {
  int finalResult;

  sync_wait(then(
      reduce_stream(
          transform_stream(
              range_stream{0, 10},
              [](int value) { return value * value; }),
          0,
          [](int state, int value) { return state + value; }),
      [&](int result) { finalResult = result; }));

  EXPECT_EQ(finalResult, 285);
  std::printf("result = %i\n", finalResult);
}

TEST(reduce_stream, Pipeable) {
  int finalResult;

  range_stream{0, 10}
    | transform_stream(
        [](int value) { return value * value; })
    | reduce_stream(
        0,
        [](int state, int value) { return state + value; })
    | then([&](int result) { finalResult = result; })
    | sync_wait();

  EXPECT_EQ(finalResult, 285);
  std::printf("result = %i\n", finalResult);
}
