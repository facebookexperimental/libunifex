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
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/on.hpp>
#include <unifex/then.hpp>
#include <unifex/let_done.hpp>
#include <unifex/sequence.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/just_from.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

TEST(TransformDone, Smoke) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  int count = 0;

  sync_wait(
    stop_when(
      sequence(
        let_done(
          schedule_after(scheduler, 200ms), 
          []{ return just(); }), 
        just_from([&]{ ++count; })),
      schedule_after(scheduler, 100ms)));

  EXPECT_EQ(count, 1);
}

TEST(TransformDone, StayDone) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  int count = 0;

  auto op = sequence(
    on(scheduler, just_done() | let_done([]{ return just(); })),
    just_from([&]{ ++count; }));
  sync_wait(std::move(op));

  EXPECT_EQ(count, 1);
}

TEST(TransformDone, Pipeable) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  int count = 0;

  sequence(
    schedule_after(scheduler, 200ms)
      | let_done(
          []{ return just(); }), 
    just_from([&]{ ++count; }))
    | stop_when(schedule_after(scheduler, 100ms))
    | sync_wait();

  EXPECT_EQ(count, 1);
}

TEST(TransformDone, WithValue) {
  auto one = 
    just_done()
    | let_done([] { return just(42); })
    | sync_wait();

  EXPECT_TRUE(one.has_value());
  EXPECT_EQ(*one, 42);

  auto multiple = 
    just_done()
    | let_done([] { return just(42, 1, 2); })
    | sync_wait();

  EXPECT_TRUE(multiple.has_value());
  EXPECT_EQ(*multiple, std::tuple(42, 1, 2));
}
