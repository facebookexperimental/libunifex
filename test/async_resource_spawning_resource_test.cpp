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

#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES
#  include <unifex/async_resource.hpp>
#  include <unifex/spawn_detached.hpp>

#  include "async_resource_test.hpp"

#  include <functional>
#  include <gtest/gtest.h>

#  include <unifex/defer.hpp>
#  include <unifex/single_thread_context.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>

using namespace unifex;
using namespace unifex_test;

namespace {

class SpawningResource {
  async_resource_ptr<UnmanagedResource> child1_;
  async_resource_ptr<UnmanagedResource> child2_;

public:
  template <typename Scheduler>
  SpawningResource(
      async_scope_ref scope,
      Scheduler sched1,
      Scheduler sched2,
      AsyncResourceTest* f) noexcept {
    // race for tear-down: child may never be constructed
    spawn_detached(
        on(sched1,
           defer([sched1, scope, f]() noexcept {
             return make_async_resource(
                 sched1, scope, [f](auto, auto) noexcept {
                   return UnmanagedResource{&(f->objectCount)};
                 });
           }) | then([this](auto&& child) noexcept { child1_.swap(child); })),
        scope);
    spawn_detached(
        on(sched2,
           defer([sched2, scope, f]() noexcept {
             return make_async_resource(
                 sched2, scope, [f](auto, auto) noexcept {
                   return UnmanagedResource{&(f->objectCount)};
                 });
           }) | then([this](auto&& child) noexcept { child2_.swap(child); })),
        scope);
  }

  // suppress deprecation warning - noop
  auto destroy() noexcept { return just(); }
};

task<void> spawning_resource(AsyncResourceTest* f) {
  single_thread_context ctx1;
  single_thread_context ctx2;
  // drop immediately
  (void)co_await make_async_resource(
      f->ctx.get_scheduler(),
      f->outerScope,
      [f, sched1 = ctx1.get_scheduler(), sched2 = ctx2.get_scheduler()](
          auto scope, auto) noexcept {
        return SpawningResource{scope, sched1, sched2, f};
      });
  co_await f->outerScope.join();
}

}  // namespace

TEST_F(AsyncResourceTest, spawning_resource) {
  sync_wait(spawning_resource(this));
}
#endif
