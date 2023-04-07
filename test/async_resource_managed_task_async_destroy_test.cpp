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

struct ManagedTaskDestroyResource : public ResourceBase {
  using ResourceBase::ResourceBase;

  task<void> destroy() noexcept {
    co_await just_from([&]() noexcept { destroyCalled_ = true; });
  }

  ~ManagedTaskDestroyResource() noexcept { EXPECT_TRUE(destroyCalled_); }

private:
  bool destroyCalled_{false};
};

task<void> managed_task_async_destroy(AsyncResourceTest* f) {
  // drop immediately
  (void)co_await make_async_resource(
      f->ctx.get_scheduler(), f->outerScope, [f](auto, auto) noexcept {
        return ManagedTaskDestroyResource{&(f->objectCount)};
      });
  co_await f->outerScope.join();
}

}  // namespace

TEST_F(AsyncResourceTest, managed_task_async_destroy) {
  sync_wait(managed_task_async_destroy(this));
}
#endif
