/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/on.hpp>
#include <unifex/transform.hpp>
#include <unifex/transform_done.hpp>
#include <unifex/sequence.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/just_with.hpp>

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
        transform_done(
          schedule_after(scheduler, 200ms), 
          []{ return just(); }), 
        just_with([&]{ ++count; })),
      schedule_after(scheduler, 100ms)));

  EXPECT_EQ(count, 1);
}

TEST(TransformDone, StayDone) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  int count = 0;

  auto op = sequence(
    on(scheduler, just_done() | transform_done([]{ return just(); })),
    just_with([&]{ ++count; }));
  sync_wait(std::move(op));

  EXPECT_EQ(count, 1);
}

TEST(TransformDone, Pipeable) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  int count = 0;

  sequence(
    schedule_after(scheduler, 200ms)
      | transform_done(
          []{ return just(); }), 
    just_with([&]{ ++count; }))
    | stop_when(schedule_after(scheduler, 100ms))
    | sync_wait();

  EXPECT_EQ(count, 1);
}

TEST(TransformDone, WithValue) {
  auto one = 
    just_done()
    | transform_done([] { return just(42); })
    | sync_wait();

  EXPECT_TRUE(one.has_value());
  EXPECT_EQ(*one, 42);

  auto multiple = 
    just_done()
    | transform_done([] { return just(42, 1, 2); })
    | sync_wait();

  EXPECT_TRUE(multiple.has_value());
  EXPECT_EQ(*multiple, std::tuple(42, 1, 2));
}
