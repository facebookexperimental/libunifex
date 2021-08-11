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
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {
constexpr auto async = [](auto& context, auto&& func) {
    return then(
        schedule_after(context.get_scheduler(), 100ms),
        (decltype(func))func);
};

constexpr auto asyncVector = [](auto& context) {
    return async(context, [] {
        std::cout << "producing vector" << std::endl;
        return std::vector<int>{1, 2, 3, 4};
    });
};
} // anonymous namespace

TEST(Let, Simple) {
  timed_single_thread_context context;

  // Simple usage of 'let_value()'
  // - defines an async scope in which the result of one async
  //   operation is in-scope for the duration of a second operation.
  std::optional<int> result =
      sync_wait(let_value(async(context, [] { return 42; }), [&](int& x) {
        printf("addressof x = %p, val = %i\n", (void*)&x, x);
        return async(context, [&]() -> int {
          printf("successor tranform\n");
          printf("addressof x = %p, val = %i\n", (void*)&x, x);
          return x;
        });
      }));

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 42);
  std::cout << "let_value done " << *result << "\n";
}

TEST(Let, Nested) {
  timed_single_thread_context context;
  // More complicated 'let_value' example that shows recursive let_value-scopes,
  // additional

  sync_wait(then(
      when_all(
          let_value(asyncVector(context),
              [&](std::vector<int>& v) {
                return async(context, [&] {
                  std::cout << "printing vector" << std::endl;
                  for (int& x : v) {
                    std::cout << x << ", ";
                  }
                  std::cout << std::endl;
                });
              }),
          let_value(just(42),
              [&](int& x) {
                return let_value(async(context, [&] { return x / 2; }), [&](int& y) {
                  return async(context, [&] { return x + y; });
                });
              })),
      [](std::variant<std::tuple<>> a, std::variant<std::tuple<int>> b) {
        std::cout << "when_all finished - [" << a.index() << ", "
                  << std::get<0>(std::get<0>(b)) << "]\n";
        EXPECT_EQ(a.index(), 0);
        EXPECT_EQ(b.index(), 0);
        EXPECT_EQ(std::get<0>(std::get<0>(b)), 63);
      }));
}

TEST(Let, Pipeable) {
  timed_single_thread_context context;

  // Simple usage of 'let_value()'
  // - defines an async scope in which the result of one async
  //   operation is in-scope for the duration of a second operation.
  std::optional<int> result = async(context, [] { return 42; })
    | let_value(
        [&](int& x) {
          printf("addressof x = %p, val = %i\n", (void*)&x, x);
          return async(context, [&]() -> int {
            printf("successor tranform\n");
            printf("addressof x = %p, val = %i\n", (void*)&x, x);
            return x;
          });
        })
    | sync_wait();

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 42);
  std::cout << "let_value done " << *result << "\n";
}
