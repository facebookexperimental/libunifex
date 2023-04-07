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

#  include <unifex/sequence.hpp>
#  include <unifex/sync_wait.hpp>

using namespace unifex;
using namespace unifex_test;

namespace {

auto plain_sender(AsyncResourceTest* f) {
  return sequence(
      make_async_resource(
          f->ctx.get_scheduler(),
          f->outerScope,
          [f](auto, auto) noexcept {
            return UnmanagedResource{&(f->objectCount)};
          }) |
          then([](auto&&...) noexcept {}),
      f->outerScope.join());
}

}  // namespace

TEST_F(AsyncResourceTest, plain_sender) {
  sync_wait(plain_sender(this));
}
#endif
