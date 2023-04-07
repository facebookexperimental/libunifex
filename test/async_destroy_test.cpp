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

#include <unifex/async_destroy.hpp>
#include <unifex/just_from.hpp>
#include <unifex/sync_wait.hpp>

#include <functional>
#include <gtest/gtest.h>

using namespace unifex;

namespace {
struct NoexceptResource {
  bool destroyed{false};
  auto destroy() noexcept {
    return just_from([&]() { destroyed = true; });
  }
};

struct ThrowingResource {
  bool destroyed{false};
  auto destroy() noexcept(false) {
    return just_from([&]() { destroyed = true; });
  }
};

struct NoexceptTagResource {
  bool destroyed{false};
  bool tagged{false};
  auto destroy() noexcept {
    return just_from([&]() { destroyed = true; });
  }

private:
  // takes precedence over destroy()
  friend auto
  tag_invoke(tag_t<async_destroy>, NoexceptTagResource& self) noexcept {
    return just_from([&]() { self.tagged = true; });
  }
};

struct ThrowingTagResource {
  bool destroyed{false};
  bool tagged{false};
  auto destroy() noexcept {
    return just_from([&]() { destroyed = true; });
  }

private:
  // takes precedence over destroy()
  friend auto
  tag_invoke(tag_t<async_destroy>, ThrowingTagResource& self) noexcept(false) {
    return just_from([&]() noexcept { self.tagged = true; });
  }
};

TEST(AsyncDestroyTest, member) {
  NoexceptResource r;
  static_assert(noexcept(async_destroy(r)));
  sync_wait(async_destroy(r));
  ASSERT_TRUE(r.destroyed);

  ThrowingResource t;
  static_assert(!noexcept(async_destroy(t)));
  sync_wait(async_destroy(t));
  ASSERT_TRUE(t.destroyed);
}

TEST(AsyncDestroyTest, tag_invoke) {
  NoexceptTagResource r;
  static_assert(noexcept(async_destroy(r)));
  sync_wait(async_destroy(r));
  ASSERT_FALSE(r.destroyed);
  ASSERT_TRUE(r.tagged);

  ThrowingTagResource t;
  static_assert(!noexcept(async_destroy(t)));
  sync_wait(async_destroy(t));
  ASSERT_FALSE(t.destroyed);
  ASSERT_TRUE(t.tagged);
}
}  // namespace
