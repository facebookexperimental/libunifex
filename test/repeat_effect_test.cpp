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
#include <unifex/when_all.hpp>
#include <unifex/just.hpp>
#include <unifex/let.hpp>
#include <unifex/transform.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/sequence.hpp>
#include <unifex/with_query_value.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

template <typename F>
auto lazy(F&& f) {
  return transform(just(), (F &&) f);
}

TEST(RepeatEffect, Smoke) {
  timed_single_thread_context context;
  inplace_stop_source done;

  auto scheduler = context.get_scheduler();

  std::atomic<int> count{0};

  sync_wait(
    when_all(
      sequence(
        schedule_after(scheduler, 500ms),
        lazy([&, stop = done.get_token()]{
          EXPECT_FALSE(stop.stop_requested());
          done.request_stop();
          EXPECT_TRUE(stop.stop_requested());
        })),
      repeat_effect(
        sequence(
          schedule_after(scheduler, 50ms), 
          lazy([&]{++count;})))), 
    done.get_token());

  EXPECT_TRUE(done.stop_requested());
  EXPECT_GT(count.load(), 1);
}
