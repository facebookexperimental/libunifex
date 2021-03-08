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

#include <unifex/async_scope.hpp>

#include <unifex/just.hpp>
#include <unifex/let_with.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sequence.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>

using unifex::async_manual_reset_event;
using unifex::async_scope;
using unifex::connect;
using unifex::get_scheduler;
using unifex::get_stop_token;
using unifex::just;
using unifex::let_with;
using unifex::schedule;
using unifex::scope_guard;
using unifex::sequence;
using unifex::single_thread_context;
using unifex::start;
using unifex::sync_wait;
using unifex::tag_t;
using unifex::transform;

struct async_scope_test : testing::Test {
  async_scope scope;
  single_thread_context thread;

  void submit_work_after_cleanup() {
    sync_wait(scope.cleanup());

    async_manual_reset_event destroyed;
    bool executed = false;

    scope.submit(
        let_with([&]() noexcept {
          return scope_guard{[&]() noexcept {
            destroyed.set();
          }};
        }, [&](auto&) noexcept {
          return transform(just(), [&]() noexcept {
            executed = true;
          });
        }),
        thread.get_scheduler());

    sync_wait(destroyed.async_wait());

    EXPECT_FALSE(executed);
  }

  void expect_work_to_run() {
    async_manual_reset_event evt;

    scope.submit(transform(just(), [&]() noexcept {
      evt.set();
    }), thread.get_scheduler());

    // we'll hang here if the above work doesn't start
    sync_wait(evt.async_wait());
  }
};

TEST_F(async_scope_test, submitting_after_cleaning_up_destroys_the_sender) {
  submit_work_after_cleanup();
}

TEST_F(async_scope_test, cleanup_is_idempotent) {
  sync_wait(scope.cleanup());

  submit_work_after_cleanup();
}

TEST_F(async_scope_test, submitting_work_makes_it_run) {
  expect_work_to_run();

  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, scope_not_stopped_until_cleanup_is_started) {
  auto cleanup = scope.cleanup();

  expect_work_to_run();

  sync_wait(std::move(cleanup));
}

TEST_F(async_scope_test, lots_of_threads_works) {
  constexpr int maxCount = 1'000;

  std::array<single_thread_context, maxCount> threads;

  async_manual_reset_event evt1, evt2, evt3;
  std::atomic<int> count{0};

  struct decr {
    decr(std::atomic<int>& count, async_manual_reset_event& evt) noexcept
        : count_(&count),
          evt_(&evt) {
    }

    decr(decr&& other) = delete;

    ~decr() {
      assert(evt_->ready());
      count_->fetch_sub(1, std::memory_order_relaxed);
    }

    std::atomic<int>* count_;
    async_manual_reset_event* evt_;
  };

  for (auto& thread : threads) {
    // Submit maxCount jobs that are all waiting on unique threads to submit a
    // job each that increments count and then waits. The last job to increment
    // count will unblock the waiting jobs, so the group will then race to tear
    // themselves down.  On tear-down, decrement count again so that it can be
    // expected to be zero once everything's done.
    //
    // This should stress-test job submission and cancellation.
    scope.submit(transform(evt1.async_wait(), [&]() noexcept {
      scope.submit(
          let_with([&] { return decr{count, evt3}; }, [&](decr&) noexcept {
            return sequence(
                transform(just(), [&]() noexcept {
                  auto prev = count.fetch_add(1, std::memory_order_relaxed);
                  if (prev + 1 == maxCount) {
                    evt2.set();
                  }
                }),
                evt3.async_wait());
          }),
          thread.get_scheduler());
    }), thread.get_scheduler());
  }

  // launch the race to submit work
  evt1.set();

  // wait until count has been incremented to maxCount
  sync_wait(evt2.async_wait());

  EXPECT_EQ(count.load(std::memory_order_relaxed), maxCount);

  // launch the race to tear down
  evt3.set();

  // wait for everyone to finish tearing down
  sync_wait(scope.cleanup());

  EXPECT_EQ(count.load(std::memory_order_relaxed), 0);
}
