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
#  include <unifex/tag_invoke.hpp>
#  include <unifex/task.hpp>

using namespace unifex;
using namespace unifex_test;

namespace {

struct ManagedDestroyTagInvokeResource : public ResourceBase {
  using ResourceBase::ResourceBase;

  ~ManagedDestroyTagInvokeResource() noexcept {
    EXPECT_FALSE(destroyCalled_);
    EXPECT_TRUE(tagInvokeCalled_);
  }

  auto destroy() noexcept {
    return just_from([&]() noexcept { destroyCalled_ = true; });
  }

private:
  // takes precedence over destroy()
  friend auto tag_invoke(
      tag_t<async_destroy>, ManagedDestroyTagInvokeResource& self) noexcept {
    return just_from([&]() noexcept { self.tagInvokeCalled_ = true; });
  }

  bool destroyCalled_{false};
  bool tagInvokeCalled_{false};
};

task<void> tag_invoke_over_async_destroy(AsyncResourceTest* f) {
  // move-only _ptr
  {
    auto ptr = co_await make_async_resource(
        f->ctx.get_scheduler(), f->outerScope, [f](auto, auto) noexcept {
          return ManagedDestroyTagInvokeResource{&(f->objectCount)};
        });
    static_assert(
        !std::is_copy_constructible_v<decltype(ptr)>,
        "ptr is a move-only type");
  }  // drop ptr
  co_await f->outerScope.join();
}

}  // namespace

TEST_F(AsyncResourceTest, tag_invoke_over_async_destroy) {
  sync_wait(tag_invoke_over_async_destroy(this));
}
#endif
