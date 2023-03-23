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
#include <unifex/just.hpp>
#include <unifex/just_from.hpp>
#include <unifex/on.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>

#include <gtest/gtest.h>
#include <thread>

using namespace unifex;

namespace {
struct Customized {
  std::thread::id id2 = std::this_thread::get_id();
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = true;

  struct op {};

  template <typename Receiver>
  op connect(Receiver&&) & {
    return op{};
  }

  template <typename Scheduler>
  friend auto tag_invoke(tag_t<on>, Scheduler&& scheduler, Customized& self) {
    return on(static_cast<Scheduler&&>(scheduler), just_from([&self] {
                self.id2 = std::this_thread::get_id();
              }));
  }
};
}  // namespace

TEST(On, Smoke) {
  auto id1 = std::this_thread::get_id();
  auto id2 = std::this_thread::get_id();

  single_thread_context thread;

  auto result = sync_wait(on(thread.get_scheduler(), just_from([&id2] {
                               id2 = std::this_thread::get_id();
                             })));
  EXPECT_NE(id1, id2);
  EXPECT_EQ(id2, thread.get_thread_id());
  EXPECT_TRUE(result.has_value());
}

TEST(On, Tag) {
  auto id1 = std::this_thread::get_id();

  single_thread_context thread;
  Customized c;
  auto result = sync_wait(on(thread.get_scheduler(), c));
  EXPECT_NE(id1, c.id2);
  EXPECT_EQ(c.id2, thread.get_thread_id());
  EXPECT_TRUE(result.has_value());
}
