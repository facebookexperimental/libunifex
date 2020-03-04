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
#include <unifex/transform.hpp>
#include <unifex/when_all.hpp>

#include <chrono>
#include <iostream>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

int main() {
  timed_single_thread_context context;

  auto async = [&](auto&& func) {
    return transform(
        schedule_after(context.get_scheduler(), 100ms),
        (decltype(func))func);
  };

  // Simple usage of 'let()'
  // - defines an async scope in which the result of one async
  //   operation is in-scope for the duration of a second operation.
  std::optional<int> result =
      sync_wait(let(async([] { return 42; }), [&](int& x) {
        printf("addressof x = %p, val = %i\n", (void*)&x, x);
        return async([&]() -> int {
          printf("successor tranform\n");
          printf("addressof x = %p, val = %i\n", (void*)&x, x);
          return x;
        });
      }));

  auto asyncVector = [&]() {
    return async([] {
      std::cout << "producing vector" << std::endl;
      return std::vector<int>{1, 2, 3, 4};
    });
  };

  // More complicated 'let' example that shows recursive let-scopes,
  // additional

  sync_wait(transform(
      when_all(
          let(asyncVector(),
              [&](std::vector<int>& v) {
                return async([&] {
                  std::cout << "printing vector" << std::endl;
                  for (int& x : v) {
                    std::cout << x << ", ";
                  }
                  std::cout << std::endl;
                });
              }),
          let(just(42),
              [&](int& x) {
                return let(async([&] { return x / 2; }), [&](int& y) {
                  return async([&] { return x + y; });
                });
              })),
      [](std::variant<std::tuple<>> a, std::variant<std::tuple<int>> b) {
        std::cout << "when_all finished - [" << a.index() << ", "
                  << std::get<0>(std::get<0>(b)) << "]\n";
      }));

  std::cout << "all done " << *result << "\n";

  return 0;
}
