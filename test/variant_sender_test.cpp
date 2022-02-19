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
#include <unifex/defer.hpp>
#include <unifex/dematerialize.hpp>
#include <unifex/just.hpp>
#include <unifex/just_error.hpp>
#include <unifex/just_done.hpp>
#include <unifex/materialize.hpp>
#include <unifex/variant_sender.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

struct IntAndStringReceiver {
    void set_value(int) {

    }

    void set_value(std::string) {

    }

    void set_done() {}

    void set_error(std::exception_ptr) noexcept {}
};

TEST(Variant, CombineJustAndError) {
  auto func = [](bool v) -> variant_sender<decltype(just(5)), decltype(just_error(5))> {
      if (v) {
          return just(5);
      } else {
          return just_error(10);
      }
  };

  auto just_variant_sender = func(true);
  EXPECT_FALSE(just_variant_sender.sends_done);
  auto result = sync_wait(just_variant_sender);

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 5);

  try {
      auto just_error_variant_sender = func(false);
      EXPECT_FALSE(just_error_variant_sender.sends_done);
      sync_wait(just_error_variant_sender);
      EXPECT_FALSE(true);
  } catch (int& v) {
      EXPECT_EQ(v, 10);
  }

  std::cout << "variant_sender done " << *result << "\n";
}

TEST(Variant, CombineJustAndDone) {
  auto func = [](bool v) -> variant_sender<decltype(just(5)), decltype(just_done())> {
      if (v) {
          return just(5);
      } else {
          return just_done();
      }
  };

  auto just_variant_sender = func(true);
  EXPECT_TRUE(just_variant_sender.sends_done);
  auto result = sync_wait(just_variant_sender);

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 5);

  auto just_done_variant_sender = func(false);
  EXPECT_TRUE(just_done_variant_sender.sends_done);
  auto result2 = sync_wait(just_done_variant_sender);
  EXPECT_FALSE(!!result2);

  std::cout << "variant_sender done " << *result << "\n";
}

TEST(Variant, CombineJustAndJust) {
  auto func = [&](bool v) -> variant_sender<decltype(just(5)), decltype(dematerialize(materialize(just(42))))> {
      if (v) {
          return just(5);
      } else {
          return dematerialize(materialize(just(42)));
      }
  };

  auto just_variant_sender = func(true);
  EXPECT_FALSE(just_variant_sender.sends_done);
  auto result = sync_wait(just_variant_sender);

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 5);

  auto materialized_variant_sender = func(false);
  auto result2 = sync_wait(materialized_variant_sender);
  EXPECT_TRUE(!!result2);
  EXPECT_EQ(*result2, 42);

  std::cout << "variant_sender done " << *result << "\n";
}

TEST(Variant, CombineJustAndJust_Invalid) {
  auto func = [](bool v) -> variant_sender<decltype(just(5)), decltype(just(std::declval<std::string>()))> {
      if (v) {
          return just(5);
      } else {
          return just(std::string("Hello World"));
      }
  };

  IntAndStringReceiver rec;

  auto just_variant_sender = func(true);
  EXPECT_FALSE(just_variant_sender.sends_done);
  auto op = unifex::connect(just_variant_sender, rec);
  unifex::start(op);

  auto just_string_sender = func(false);
  EXPECT_FALSE(just_variant_sender.sends_done);
  auto op2 = unifex::connect(just_string_sender, rec);
  unifex::start(op2);
}
