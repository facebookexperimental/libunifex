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

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {
template <typename F>
auto lazy(F&& f) {
  return transform(just(), (F &&) f);
}
} // anonymous namespace

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
        lazy([&]{ ++count; })),
      schedule_after(scheduler, 100ms)));

  EXPECT_EQ(count, 1);
}

TEST(TransformDone, StayDone) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  int count = 0;

  sequence(
    just_done()
      | transform_done(
          []{ return just(); }) 
      | on(scheduler),
    lazy([&]{ ++count; }))
    | sync_wait();

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
    lazy([&]{ ++count; }))
    | stop_when(schedule_after(scheduler, 100ms))
    | sync_wait();

  EXPECT_EQ(count, 1);
}
