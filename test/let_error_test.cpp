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
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/just_from.hpp>
#include <unifex/let_done.hpp>
#include <unifex/let_error.hpp>
#include <unifex/on.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sequence.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#if !UNIFEX_NO_COROUTINES
#  include <unifex/task.hpp>
#endif  // !UNIFEX_NO_COROUTINES
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

TEST(TransformError, Smoke) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  int count = 0;

  sync_wait(stop_when(
      sequence(
          let_error(
              let_done(
                  schedule_after(scheduler, 200ms),
                  [] { return just_error(-1); }),
              [](auto&&) { return just(); }),
          just_from([&] { ++count; })),
      schedule_after(scheduler, 100ms)));

  EXPECT_EQ(count, 1);
}

TEST(TransformError, StayError) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  int count = 0;

  auto op = sequence(
      on(scheduler,
         just_error(42)  //
             | let_error([](auto&&) { return just(); })),
      just_from([&] { ++count; }));
  sync_wait(std::move(op));

  EXPECT_EQ(count, 1);
}

TEST(TransformError, Pipeable) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  int count = 0;

  sequence(
      schedule_after(scheduler, 200ms)               //
          | let_done([] { return just_error(-1); })  //
          | let_error([](auto&&) { return just(); }),
      just_from([&] { ++count; }))                   //
      | stop_when(schedule_after(scheduler, 100ms))  //
      | sync_wait();

  EXPECT_EQ(count, 1);
}

TEST(TransformError, WithValue) {
  auto one = just_error(-1) |                     //
      let_error([](auto&&) { return just(42); })  //
      | sync_wait();

  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(*one, 42);

  auto multiple = just_error(-1)                          //
      | let_error([](auto&&) { return just(42, 1, 2); })  //
      | sync_wait();

  ASSERT_TRUE(multiple.has_value());
  EXPECT_EQ(*multiple, std::tuple(42, 1, 2));
}

TEST(TransformError, Throw) {
  auto one = just_from([]() -> int { throw -1; })   //
      | let_error([](auto&&) { return just(42); })  //
      | sync_wait();

  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(*one, 42);
}

namespace {
struct just_int {
  template <typename Unused>
  auto operator()(Unused&&) {
    return just(0);
  }
  auto operator()(int val) { return just(val); }
};
}  // namespace

TEST(TransformError, JustError) {
  auto one = just_error(-1)                               //
      | let_error([](auto&&) { return just_error(42); })  //
      | let_error(just_int{})                             //
      | sync_wait();

  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(*one, 42);
}

TEST(TransformError, SequenceRef) {
  auto one = just_error(42)  //
      | let_error([](auto& e) mutable {
               return sequence(just_from([] {}), just_error(std::move(e)));
             })                //
      | let_error(just_int{})  //
      | sync_wait();
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(*one, 42);
}

TEST(TransformError, SequenceVal) {
  auto one = just_error(42)  //
      | let_error([](auto e) mutable {
               return sequence(just_from([] {}), just_error(std::move(e)));
             })                //
      | let_error(just_int{})  //
      | sync_wait();
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(*one, 42);
}

TEST(TransformError, SequenceFwd) {
  auto one = just_error(42)  //
      | let_error([](auto&& e) mutable {
               return sequence(
                   just_from([] {}), just_error(static_cast<decltype(e)&&>(e)));
             })                //
      | let_error(just_int{})  //
      | sync_wait();
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(*one, 42);
}

#if !UNIFEX_NO_COROUTINES
TEST(TransformError, WithTask) {
  auto value = let_error(
                   then(
                       []() -> task<int> {
                         co_return 41;
                       }(),
                       [](auto) { return 42; }),
                   [](auto&&) { return just(-1); })  //
      | let_done([]() { return just(-2); })          //
      | sync_wait();

  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, 42);

#  if !UNIFEX_NO_EXCEPTIONS
  auto error = let_error(
                   then(
                       []() -> task<int> {
                         throw std::runtime_error("");
                         co_return 41;
                       }(),
                       [](auto) { return 42; }),
                   [](auto&&) { return just(-1); })  //
      | let_done([]() { return just(-2); })          //
      | sync_wait();

  EXPECT_TRUE(error.has_value());
  EXPECT_EQ(*error, -1);
#  endif  // !UNIFEX_NO_EXCEPTIONS

  auto done = let_error(
                  then(
                      []() -> task<int> {
                        co_await just_done();
                        co_return 41;
                      }(),
                      [](auto) { return 42; }),
                  [](auto&&) { return just(-1); })  //
      | let_done([]() { return just(-2); })         //
      | sync_wait();

  EXPECT_TRUE(done.has_value());
  EXPECT_EQ(*done, -2);
}
#endif  // !UNIFEX_NO_COROUTINES
