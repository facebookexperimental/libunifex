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
#include <unifex/sync_wait.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/reduce_stream.hpp>
#include <unifex/transform.hpp>
#include <unifex/range_stream.hpp>

#include <cstdio>

using namespace unifex;

int main() {

  int finalResult;
  sync_wait(transform(
      reduce_stream(
          transform_stream(
              range_stream{0, 10},
              [](int value) { return value * value; }),
          0,
          [](int state, int value) { return state + value; }),
      [&](int result) { finalResult = result; }));

  std::printf("result = %i\n", finalResult);

  return 0;
}
