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

#  include <gtest/gtest.h>

#  include <unifex/just.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>
#  include <tuple>

using namespace unifex;
using namespace unifex_test;

namespace {

class NoCopyNoMoveResource {
  int i_;
  double d_;
  std::string s_;

public:
  NoCopyNoMoveResource(int i, double d, std::string s) noexcept
    : i_(i)
    , d_(d)
    , s_(std::move(s)) {}

  NoCopyNoMoveResource(const NoCopyNoMoveResource&) = delete;
  NoCopyNoMoveResource(NoCopyNoMoveResource&&) = delete;

  auto args() const noexcept { return std::make_tuple(i_, d_, s_); }

  // suppress deprecation warning - noop
  auto destroy() noexcept { return just(); }
};

task<void> in_place(AsyncResourceTest* f) {
  {
    auto r = co_await make_async_resource<NoCopyNoMoveResource>(
        f->ctx.get_scheduler(), f->outerScope, [](auto, auto) noexcept {
          return just(42, 42.42, "Fish");
        });
    EXPECT_EQ(r->args(), std::make_tuple(42, 42.42, "Fish"));
  }
  co_await f->outerScope.join();
}

}  // namespace

TEST_F(AsyncResourceTest, in_place) {
  sync_wait(in_place(this));
}
#endif
