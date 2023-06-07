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
#include <unifex/async_manual_reset_event.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/just_from.hpp>
#include <unifex/nest.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/spawn_detached.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/v1/async_scope.hpp>
#include <unifex/v1/debug_async_scope.hpp>
#include <unifex/v2/async_scope.hpp>
#include <unifex/v2/debug_async_scope.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::literals;

// guaranteed to deadlock, useful for validating operation states can be
// inspected in a debugger
// use --gtest_also_run_disabled_tests to run manually
TEST(Debug, DISABLED_SyncWaitDeadlockV1) {
  v1::debug_async_scope scope;
  spawn_detached(
      just_from([&scope]() noexcept {  //
        sync_wait(scope.complete());   //
      }),
      scope);
}

TEST(Debug, DISABLED_SyncWaitDeadlockV1Meth) {
  v1::debug_async_scope scope;
  scope.detached_spawn(just_from([&scope]() noexcept {  //
    sync_wait(scope.complete());                        //
  }));
}

TEST(Debug, DISABLED_SyncWaitDeadlockV1TooLate) {
  single_thread_context ctx;
  v1::debug_async_scope scope;
  async_manual_reset_event evt;
  std::atomic_bool scheduled{false};

  // wait for evt to be set on a background thread; note that the `async_wait()`
  // _Sender_ is unstoppable
  auto fut = scope.spawn_on(
      ctx.get_scheduler(),
      sequence(
          just_from([&scheduled]() noexcept { scheduled = true; }),
          evt.async_wait()));

  // wait for the scheduled op to be started
  while (!scheduled) {
    continue;
  }
  // send a stop request to all the Senders spawned within the scope; this will
  // trigger the future to cancel itself, but not the unstoppable `async_wait()`
  scope.request_stop();

  // with the scope joined, pending futures should all immediately
  // complete with done. result has no value.
  auto result = sync_wait(std::move(fut));
  ASSERT_FALSE(result.has_value());

  // but the scope itself won't complete until the spawned work is actually
  // done so we will be stuck waiting for the event to be signaled
  sync_wait(scope.cleanup());

  // it's too late. this should be called before `scope.cleanup()`
  evt.set();
}

TEST(Debug, DISABLED_SyncWaitDeadlockV2) {
  v2::debug_async_scope scope;
  spawn_detached(
      just_from([&scope]() noexcept {  //
        sync_wait(scope.join());       //
      }),
      scope);
}
