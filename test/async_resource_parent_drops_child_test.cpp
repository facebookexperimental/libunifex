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

task<void> parent_drops_child(AsyncResourceTest* f) {
  // drop immediately
  (void)co_await make_async_resource<
      SingleNestingResource<UnmanagedResource>>(  // parent
      f->ctx.get_scheduler(),
      f->outerScope,
      [f](auto scope, auto scheduler) noexcept {
        return make_async_resource(  // child
            scheduler,
            scope,
            [f](auto, auto) noexcept {
              return UnmanagedResource{&(f->objectCount)};
            });
      });
  co_await f->outerScope.join();  // grandparent
}

}  // namespace

TEST_F(AsyncResourceTest, parent_drops_child) {
  sync_wait(parent_drops_child(this));
}
#endif
