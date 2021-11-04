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
 * See the License for the specific language go4verning permissions and
 * limitations under the License.
 */
#include <unifex/just.hpp>
#include <unifex/let_done.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sequence.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;

template <typename R>
struct CpoTestSenderOp {
  void start() noexcept { set_value(std::move(rec), 12); }

  R rec;
};

struct CpoTestSenderSyncWaitR {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<int>>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = false;

  friend auto tag_invoke(tag_t<sync_wait_r<int>>, CpoTestSenderSyncWaitR) {
    return std::make_optional<int>(42);
  }

  template <typename Receiver>
  friend auto
  tag_invoke(tag_t<connect>, CpoTestSenderSyncWaitR, Receiver&& rec) {
    return CpoTestSenderOp<Receiver>{(Receiver &&) rec};
  }
};

struct CpoTestSenderSyncWait {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<int>>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = false;

  friend auto tag_invoke(tag_t<sync_wait>, CpoTestSenderSyncWait) {
    return std::make_optional<int>(42);
  }

  template <typename Receiver>
  friend auto
  tag_invoke(tag_t<connect>, CpoTestSenderSyncWait, Receiver&& rec) {
    return CpoTestSenderOp<Receiver>{(Receiver &&) rec};
  }
};

TEST(SyncWait, CpoSyncWaitR) {
  // CpoTestSenderSyncWaitR redefines `sync_wait_r<int>` and this also affects
  // `sync_wait`.
  std::optional<int> i = sync_wait_r<int>(CpoTestSenderSyncWaitR{});
  ASSERT_TRUE(i.has_value());
  EXPECT_EQ(*i, 42);

  std::optional<int> j = sync_wait(CpoTestSenderSyncWaitR{});
  ASSERT_TRUE(j.has_value());
  EXPECT_EQ(*j, 42);
}

TEST(SyncWait, CpoSyncWaitRPiped) {
  // CpoTestSenderSyncWaitR redefines `sync_wait_r<int>` and this also affects
  // `sync_wait`.
  std::optional<int> i = CpoTestSenderSyncWaitR{} | sync_wait_r<int>();
  ASSERT_TRUE(i.has_value());
  EXPECT_EQ(*i, 42);

  std::optional<int> j = CpoTestSenderSyncWaitR{} | sync_wait();
  ASSERT_TRUE(j.has_value());
  EXPECT_EQ(*j, 42);
}

TEST(SyncWait, CpoSyncWait) {
  // CpoTestSenderSyncWaitR redefines `sync_wait` and this does not affects
  // `sync_wait_r<int>` which gets the default behaviour of `sync_wait_r`.
  std::optional<int> i = sync_wait_r<int>(CpoTestSenderSyncWait{});
  ASSERT_TRUE(i.has_value());
  EXPECT_EQ(*i, 12);

  std::optional<int> j = sync_wait(CpoTestSenderSyncWait{});
  ASSERT_TRUE(j.has_value());
  EXPECT_EQ(*j, 42);
}

TEST(SyncWait, CpoSyncWaitPiped) {
  // CpoTestSenderSyncWaitR redefines `sync_wait` and this does not affects
  // `sync_wait_r<int>` which gets the default behaviour of `sync_wait_r`.
  std::optional<int> i = CpoTestSenderSyncWait{} | sync_wait_r<int>();
  ASSERT_TRUE(i.has_value());
  EXPECT_EQ(*i, 12);

  std::optional<int> j = CpoTestSenderSyncWait{} | sync_wait();
  ASSERT_TRUE(j.has_value());
  EXPECT_EQ(*j, 42);
}
