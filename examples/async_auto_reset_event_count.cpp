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
#include <unifex/async_auto_reset_event.hpp>
#include <unifex/reduce_stream.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>

#include <cstdio>

using namespace unifex;

int main() {
  async_auto_reset_event evt{true};
  sync_wait(then(
      reduce_stream(
          evt.stream(),
          0,
          [&evt](int count) {
            std::printf("got %i\n", count);
            if (count < 2) {
              evt.set();
            } else {
              evt.set_done();
            }
            return ++count;
          }),
      [](int result) { std::printf("result %d\n", result); }));

  return 0;
}
