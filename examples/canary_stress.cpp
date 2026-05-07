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

#if __cplusplus >= 201911L

#  include <unifex/async_scope.hpp>
#  include <unifex/canary.hpp>
#  include <unifex/create_raw_sender.hpp>
#  include <unifex/on.hpp>
#  include <unifex/single_thread_context.hpp>
#  include <unifex/sync_wait.hpp>

#  include <atomic>
#  include <cassert>
#  include <iostream>

using namespace unifex;

// Stress tests for unifex::canary with create_raw_sender.
//
// A sender is run on start_ctx (via on()). Inside start(), async
// work is dispatched to completion_ctx which completes the receiver —
// potentially destroying the op state before start() returns. The
// canary detects this race.
//
// Two tests:
//   1. Race test: alive() races against async completion.
//      Both "alive" and "dead" outcomes are valid.
//   2. Guard test: guard is acquired before the async call,
//      blocking ~canary() until the guard is released.

static constexpr int iterations = 10000;

void stress_race() {
  single_thread_context start_ctx;
  single_thread_context completion_ctx;
  async_scope scope;

  std::atomic<int> guard_alive{0};

  for (int i = 0; i < iterations; ++i) {
    sync_wait(on(
        start_ctx.get_scheduler(), create_raw_sender<int>([&](auto&& receiver) {
          // The factory returns a plain callable (operator() = start).
          return [receiver = std::forward<decltype(receiver)>(receiver),
                  &scope,
                  &completion_ctx,
                  &guard_alive,
                  c = canary{}]() mutable noexcept {
            auto watcher = c.watch();

            // Launch async completion on a different thread.
            scope.detached_spawn_call_on(
                completion_ctx.get_scheduler(),
                [&receiver]() noexcept { set_value(std::move(receiver), 42); });

            // Race: did completion destroy us?
            // IMPORTANT: only access captures (which live in the
            // op state) inside the guard. When alive() returns
            // false, the op state is destroyed — touching any
            // capture would be UAF.
            if (auto guard = watcher.alive()) {
              guard_alive.fetch_add(1, std::memory_order_relaxed);
            }
          };
        })));
  }

  sync_wait(scope.cleanup());

  int alive = guard_alive.load(std::memory_order_relaxed);
  int dead = iterations - alive;
  std::cout << "stress_race: " << iterations << " iterations, " << alive
            << " alive, " << dead << " dead\n";
}

void stress_guard() {
  single_thread_context start_ctx;
  single_thread_context completion_ctx;
  async_scope scope;

  std::atomic<int> post_guard_writes{0};

  for (int i = 0; i < iterations; ++i) {
    sync_wait(on(
        start_ctx.get_scheduler(), create_raw_sender<int>([&](auto&& receiver) {
          return [receiver = std::forward<decltype(receiver)>(receiver),
                  &scope,
                  &completion_ctx,
                  &post_guard_writes,
                  c = canary{}]() mutable noexcept {
            auto watcher = c.watch();

            // Acquire guard BEFORE launching async work.
            auto guard = watcher.alive();

            scope.detached_spawn_call_on(
                completion_ctx.get_scheduler(),
                [&receiver]() noexcept { set_value(std::move(receiver), 42); });

            // Guard blocks ~canary(). Safe to write.
            if (guard) {
              post_guard_writes.fetch_add(1, std::memory_order_relaxed);
            }
          };
        })));
  }

  sync_wait(scope.cleanup());

  int writes = post_guard_writes.load(std::memory_order_relaxed);
  assert(writes == iterations);
  std::cout << "stress_guard: " << iterations << " iterations, " << writes
            << " guarded writes\n";
}

int main() {
  stress_race();
  stress_guard();
  std::cout << "PASSED\n";
}

#else

int main() {
}

#endif
