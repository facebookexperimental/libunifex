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

#  include "async_resource_test.hpp"

#  include <functional>
#  include <gtest/gtest.h>

#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>

using namespace unifex;
using namespace unifex_test;

namespace {

template <typename Scope, typename Scheduler>
struct Proxy {
  Scope scope;
  Scheduler scheduler;
  TwinNestingResource resource;

  // suppress deprecation warning - noop
  auto destroy() noexcept { return just(); }
};

task<void> two_children_later(AsyncResourceTest* f) {
  {
    auto parent = co_await make_async_resource(  // parent Proxy
        f->ctx.get_scheduler(),
        f->outerScope,
        [](auto scope, auto scheduler) noexcept {
          return Proxy<decltype(scope), decltype(scheduler)>{
              scope, scheduler, TwinNestingResource{}};
        });
    // children
    auto child1 = co_await make_async_resource(
        parent->scheduler, parent->scope, [f](auto, auto) noexcept {
          return UnmanagedResource{&(f->objectCount)};
        });
    parent->resource.swap1(std::move(child1));
    auto child2 = co_await make_async_resource(
        parent->scheduler, parent->scope, [f](auto, auto) noexcept {
          return UnmanagedResource{&(f->objectCount)};
        });
    parent->resource.swap2(std::move(child2));
  }
  co_await f->outerScope.join();  // grandparent
}

}  // namespace

TEST_F(AsyncResourceTest, two_children_later) {
  sync_wait(two_children_later(this));
}
#endif
