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
#include <unifex/just.hpp>
#include <unifex/let.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/find_if.hpp>
#include <unifex/transform.hpp>
#include <unifex/when_all.hpp>

#include <chrono>
#include <iostream>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

int main() {
  std::vector<int> input{1, 2, 3, 4};
  // Apply linear find_if.
  // As for std::find_if it returns the first instance that matches the
  // predicate where the algorithm takes an iterator pair as the first
  // two arguments, and forwards all other arguments to the predicate and
  // onwards to the result.
  // Precise API shape for the data being passed through is TBD, this is
  // one option only.
  std::optional<std::vector<int>::iterator> result = sync_wait(
    transform(
      find_if(
          just(begin(input), end(input), 3),
          [&](const int& v, int another_parameter) {
            return v == another_parameter;
          }),
      [](std::vector<int>::iterator v, int another_parameter) {
        assert(another_parameter == 3);
        return v;
      }));

  std::cout << "all done " << **result << "\n";

  return 0;
}
