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

// Benchmark: v1 vs v2 async_manual_reset_event
//
// Ping-pong (low contention):
//   1 generator + 1 listener, 2 events, 2 threads.
//   Measures round-trip latency of set + async_wait + reset.
//
// Shared event (high contention):
//   1 signaller doing set/reset cycles on a single event,
//   4 waiter tasks doing async_wait() in a loop.
//   Stresses push_back contention (multiple waiters registering
//   simultaneously) and push vs drain contention (waiters
//   registering while set() drains the list).
//
// By default uses pure sender pipelines for the ping-pong test
// (no coroutine overhead).  Define AMRE_BENCH_COROUTINES to use
// coroutine-based implementation instead (requires coroutine
// support).
//
// Deadlock watchdog: a standalone std::thread sleeps for 5 minutes,
// then dumps diagnostic state (per-task step + cycle, event ready
// flags) to stderr and calls std::terminate().  This is independent
// of the unifex machinery under test.

#include <unifex/async_manual_reset_event.hpp>
#include <unifex/defer.hpp>
#include <unifex/let_value.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/v2/async_manual_reset_event.hpp>
#include <unifex/when_all.hpp>
#include <unifex/with_query_value.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>

using namespace unifex;
using bench_clock = std::chrono::steady_clock;

template <typename Event, typename Scheduler>
auto async_wait_on(Event& evt, Scheduler sched) {
  return with_query_value(evt.async_wait(), get_scheduler, sched);
}

// ---- Ping-pong implementation selection -----------------------------------
//
// Generator: set(ping) -> wait(pong) -> reset(pong) -> loop
// Listener:  wait(ping) -> reset(ping) -> set(pong) -> loop
//
// with_query_value overrides get_scheduler so that async_wait
// reschedules onto the designated thread.

template <typename Event, typename Scheduler>
auto gen(Event& ping, Event& pong, Scheduler sched, int n) {
  return repeat_effect_until(
      defer([&ping, &pong, sched] {
        ping.set();
        return async_wait_on(pong, sched) | then([&pong] { pong.reset(); });
      }),
      [n, i = 0]() mutable { return ++i >= n; });
}

template <typename Event, typename Scheduler>
auto listen(Event& ping, Event& pong, Scheduler sched, int n) {
  return repeat_effect_until(
      defer([&ping, &pong, sched] {
        return async_wait_on(ping, sched) | then([&ping, &pong] {
                 ping.reset();
                 pong.set();
               });
      }),
      [n, i = 0]() mutable { return ++i >= n; });
}

// ---- Ping-pong: 1 pair (2 threads) ---------------------------------------

template <typename Event>
void run_pingpong(int n) {
  Event ping;
  Event pong;
  single_thread_context c0, c1;

  sync_wait(when_all(
      gen(ping, pong, c0.get_scheduler(), n),
      listen(ping, pong, c1.get_scheduler(), n)));
}

// ---- Shared event: 1 signaller + 4 waiters (5 threads) -------------------
//
// Signaller: n set/reset cycles on a shared event.
// Waiters:   async_wait(evt) in a loop via sender pipelines.
//
// Each cycle has two ack rounds:
//   1. Signaller: evt.set()           -> drains waiter list
//   2. Waiters:   wake, ack           -> last sets ack_event
//   3. Signaller: wait ack_event, reset ack, evt.reset(),
//                 release_event.set() -> tells waiters to proceed
//   4. Waiters:   wait release_event, ack -> last sets ack_event
//   5. Signaller: wait ack_event, reset ack,
//                 release_event.reset() -> safe: all waiters passed
//
// Two ack rounds ensure the signaller waits for ALL waiters
// before resetting both evt and release_event, preventing
// the set/reset Dekker race in scheduler-affine implementations.
//
// Termination: signaller sets done + evt + release_event and
// exits.  Waiters stuck on either event wake up, and the done
// predicate terminates their loops.  No final ack round -- a
// waiter that already exited (predicate saw done=true) would
// never ack, causing a deadlock.
//
// All tasks run in a single sync_wait(when_all(...)) with each
// task pinned to its own single_thread_context scheduler.
//
// This exercises real contention:
//   - Step 1->2: multiple waiters call push_back simultaneously
//   - Step 1:    set() drains while late waiters may still push

template <typename Event>
void run_contention(int n) {
  Event evt;            // event under test
  Event ack_event;      // last acker -> signaller
  Event release_event;  // signaller -> waiters: evt is reset
  std::atomic<int> ack{0};
  std::atomic<bool> done{false};

  constexpr int num_waiters = 4;

  // ---- Execution contexts ----

  single_thread_context ctx[num_waiters + 1];

  auto do_ack = [&] {
    if (ack.fetch_add(1, std::memory_order_acq_rel) == num_waiters - 1) {
      ack_event.set();
    }
  };

  auto reset_ack = [&] {
    ack_event.reset();
    ack.store(0, std::memory_order_relaxed);
  };

  auto sig_sched = ctx[0].get_scheduler();

  // Signaller: n cycles, then final wake for termination.
  auto signaller =
      repeat_effect_until(
          defer([&] {
            evt.set();
            return async_wait_on(ack_event, sig_sched) | then([&] {
                     reset_ack();
                     evt.reset();
                     release_event.set();
                   }) |
                let_value([&] {
                     return async_wait_on(ack_event, sig_sched) | then([&] {
                              reset_ack();
                              release_event.reset();
                            });
                   });
          }),
          [n, i = 0]() mutable { return ++i >= n; }) |
      then([&] {
        done.store(true, std::memory_order_release);
        evt.set();
        release_event.set();
      });

  // Waiter: loop until done, acking twice per cycle.
  auto make_waiter = [&](int idx) {
    auto sched = ctx[idx + 1].get_scheduler();
    return repeat_effect_until(
        defer([&, sched] {
          return async_wait_on(evt, sched) | then(do_ack) |
              let_value([&, sched] {
                   return async_wait_on(release_event, sched) | then(do_ack);
                 });
        }),
        [&] { return done.load(std::memory_order_acquire); });
  };

  sync_wait(when_all(
      std::move(signaller),
      make_waiter(0),
      make_waiter(1),
      make_waiter(2),
      make_waiter(3)));
}

// ---- Time-bounded benchmarking -------------------------------------------
//
// Runs the benchmark in fixed-size batches, accumulating iterations
// until the target duration is reached.  This avoids the calibration
// pitfall where a quiet warm-up period leads to an oversized N that
// cannot complete under heavy load (common on shared CI runners).

static constexpr auto bench_duration = std::chrono::seconds(1);

// Keep batch size small so the time-bounded loop can exit promptly.
// Each ping-pong iteration involves cross-thread round-trips whose
// latency can be 10-100ms on overloaded CI VMs.
static constexpr int pingpong_batch = 10;

// Contention batch can be larger: the signaller does set/reset in
// a tight loop on one thread, so individual cycles are fast.
// Thread contexts are created once per batch.
static constexpr int contention_batch = 1000;

template <typename Fn>
void bench(const char* label, Fn fn, int batch) {
  int total = 0;
  auto t0 = bench_clock::now();
  bench_clock::duration elapsed;

  do {
    fn(batch);
    total += batch;
    elapsed = bench_clock::now() - t0;
  } while (elapsed < bench_duration);

  auto elapsed_ns = static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());

  std::printf(
      "  %-8s %8d iters  %8.0f ns/iter\n", label, total, elapsed_ns / total);
}

int main() {
  using v1_event = async_manual_reset_event;
  using v2_event = v2::async_manual_reset_event;

  std::printf("Ping-pong (1 generator, 1 listener):\n");
  bench("v1", [&](int n) { run_pingpong<v1_event>(n); }, pingpong_batch);
  bench("v2", [&](int n) { run_pingpong<v2_event>(n); }, pingpong_batch);

  std::printf("\nContention (1 signaller, 4 waiters, shared event):\n");
  bench("v1", [&](int n) { run_contention<v1_event>(n); }, contention_batch);
  bench("v2", [&](int n) { run_contention<v2_event>(n); }, contention_batch);

  return 0;
}
