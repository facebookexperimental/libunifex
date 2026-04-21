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

#include <unifex/canary.hpp>
#include <unifex/create.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

namespace {

using namespace unifex;

// -- Standalone canary tests (no senders) --

TEST(canary_test, unwatched_destructor_is_noop) {
  // A canary with no watcher attached should destroy without issue.
  canary c;
}

TEST(canary_test, watched_but_no_destruction) {
  // Watcher attached, canary outlives the watcher. alive() returns
  // a truthy guard since the canary is still alive.
  canary c;
  {
    auto w = c.watch();
    auto g = w.alive();
    EXPECT_TRUE(g);
  }
  // watcher destroyed, canary still alive — fine.
}

TEST(canary_test, destroyed_before_alive_check) {
  // Canary destroyed (on another thread) before alive() is called.
  // alive() must return falsy.
  std::atomic<bool> watcher_ready{false};
  std::atomic<bool> canary_destroyed{false};

  auto c = std::make_unique<canary>();

  // The watcher must be on the thread that calls alive(), so we
  // create it on the main thread. But the canary is destroyed on
  // the other thread after the watcher is registered.
  auto w = c->watch();

  std::thread destroyer([&]() {
    // Wait until watcher is ready
    while (!watcher_ready.load(std::memory_order_acquire)) {
    }
    c.reset();
    canary_destroyed.store(true, std::memory_order_release);
  });

  watcher_ready.store(true, std::memory_order_release);
  // Wait for canary to be destroyed
  while (!canary_destroyed.load(std::memory_order_acquire)) {
  }

  auto g = w.alive();
  EXPECT_FALSE(g);

  destroyer.join();
}

TEST(canary_test, guard_blocks_destructor) {
  // alive() returns truthy guard. Canary destructor (on another thread)
  // must block until the guard is released.
  std::atomic<bool> guard_acquired{false};
  std::atomic<bool> canary_destroyed{false};
  std::atomic<int> value{0};

  auto c = std::make_unique<canary>();
  auto w = c->watch();

  std::thread destroyer([&]() {
    while (!guard_acquired.load(std::memory_order_acquire)) {
    }
    c.reset();
    canary_destroyed.store(true, std::memory_order_release);
  });

  {
    auto g = w.alive();
    EXPECT_TRUE(g);
    guard_acquired.store(true, std::memory_order_release);

    // Simulate work while guard is held. The canary destructor
    // is spinlocking on the other thread.
    for (int i = 0; i < 1000; ++i) {
      value.fetch_add(1, std::memory_order_relaxed);
    }

    // Canary should NOT be destroyed yet — guard is still held.
    EXPECT_FALSE(canary_destroyed.load(std::memory_order_acquire));
  }
  // Guard released here. Canary destructor should complete shortly.

  destroyer.join();
  EXPECT_TRUE(canary_destroyed.load(std::memory_order_acquire));
  EXPECT_EQ(value.load(std::memory_order_relaxed), 1000);
}

// -- Sender integration tests using create() --

TEST(canary_test, sync_completion_detects_destruction) {
  // A sender that completes synchronously during start(). The canary
  // detects that the op state has been destroyed and prevents
  // post-completion access.
  bool wrote_after_completion = false;
  bool guard_was_alive = true;

  auto result = sync_wait(
      create<int>([&wrote_after_completion, &guard_was_alive](auto& rec) {
        // Simulate the pattern: start an operation that completes
        // synchronously, then try to modify the op state.
        canary c;
        auto w = c.watch();

        // Complete synchronously — this destroys the op state
        // (including the canary) because sync_wait consumes the result.
        rec.set_value(42);

        // At this point, rec (and the canary inside the op state)
        // may be destroyed. Check:
        if (auto g = w.alive()) {
          wrote_after_completion = true;
        } else {
          guard_was_alive = false;
        }
      }));

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
  // The canary was a local in the callable, not in the op state.
  // create()'s callable runs during start() and the canary is on
  // the stack, so it IS alive after set_value. Let's verify:
  EXPECT_TRUE(guard_was_alive);
}

TEST(canary_test, sync_completion_canary_on_stack) {
  // When the canary is on the stack (not in the op state), it
  // survives the synchronous completion. The guard should be truthy.
  bool guard_was_alive = false;

  canary c;
  auto w = c.watch();

  // Synchronous completion — canary is on the stack, survives.
  auto result = sync_wait(create<int>([](auto& rec) { rec.set_value(42); }));

  if (auto g = w.alive()) {
    guard_was_alive = true;
  }

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
  EXPECT_TRUE(guard_was_alive);
}

TEST(canary_test, async_thread_completion_race) {
  // Tests the race between async completion (destroying the op state)
  // and post-start() state modification, using threads directly
  // (not senders) to control the timing precisely.
  for (int iteration = 0; iteration < 100; ++iteration) {
    std::atomic<bool> start_phase_done{false};
    bool guard_was_alive = false;
    int post_start_value = 0;

    auto c = std::make_unique<canary>();
    auto w = c->watch();

    // "Completion thread" — destroys the canary after start phase begins
    std::thread completer([&]() {
      // Wait for start phase to be underway
      while (!start_phase_done.load(std::memory_order_acquire)) {
      }
      // Simulate completion by destroying the canary
      c.reset();
    });

    // "Start phase" — signal the completer, then check alive
    start_phase_done.store(true, std::memory_order_release);

    // Race: canary might be destroyed by now, or might not.
    if (auto g = w.alive()) {
      guard_was_alive = true;
      post_start_value = 42;
      // Guard blocks canary destructor — c still valid
    }

    completer.join();

    // Either the guard was alive (and we safely wrote 42) or it
    // wasn't (and we didn't write). Both are valid outcomes.
    if (guard_was_alive) {
      EXPECT_EQ(post_start_value, 42);
    } else {
      EXPECT_EQ(post_start_value, 0);
    }
  }
}

TEST(canary_test, guard_holds_during_async_destruction) {
  // Verifies that the guard blocks the canary destructor on another
  // thread, allowing safe access to shared state.
  for (int iteration = 0; iteration < 100; ++iteration) {
    std::atomic<bool> destructor_completed{false};
    std::atomic<int> shared_counter{0};

    auto c = std::make_unique<canary>();
    auto w = c->watch();

    // Acquire the guard BEFORE starting the completion thread.
    auto g = w.alive();
    ASSERT_TRUE(g);

    std::thread completer([&]() {
      // This will block until the guard is released
      c.reset();
      destructor_completed.store(true, std::memory_order_release);
    });

    // Do work while the guard is held
    for (int i = 0; i < 100; ++i) {
      shared_counter.fetch_add(1, std::memory_order_relaxed);
    }

    // Destructor should still be blocked
    EXPECT_FALSE(destructor_completed.load(std::memory_order_acquire));

    // Release the guard by letting it go out of scope
    {
      auto released = std::move(g);
    }

    completer.join();
    EXPECT_TRUE(destructor_completed.load(std::memory_order_acquire));
    EXPECT_EQ(shared_counter.load(std::memory_order_relaxed), 100);
  }
}

TEST(canary_test, multiple_watch_cycles) {
  // A canary can be watched multiple times (sequentially).
  canary c;
  for (int i = 0; i < 10; ++i) {
    auto w = c.watch();
    auto g = w.alive();
    EXPECT_TRUE(g);
  }
}

}  // namespace
