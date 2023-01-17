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
#include <unifex/any_sender_of.hpp>
#include <unifex/just_done.hpp>
#include <unifex/let_value.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/let_value_with_stop_token.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/when_all.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

struct FusedStopSource : testing::Test {};

namespace {
template <typename StopToken, typename Callback>
auto make_stop_callback(StopToken stoken, Callback callback) {
  using stop_callback_t = typename StopToken::template callback_type<Callback>;

  return stop_callback_t{stoken, std::move(callback)};
}
}  // namespace

TEST_F(FusedStopSource, DefaultCallback) {
  inplace_stop_source input;
  fused_stop_source<inplace_stop_token> fuser;
  // no custom callbacks are registered on token
  fuser.register_callbacks(input.get_token());
  input.request_stop();

  ASSERT_TRUE(input.stop_requested());
  // stop request propagates to fuser
  ASSERT_TRUE(fuser.stop_requested());
}

TEST_F(FusedStopSource, OmitRegisterCallbackDownwards) {
  int external_context = 0;
  inplace_stop_source input;
  fused_stop_source<inplace_stop_token> fuser;
  auto callback =
      make_stop_callback(input.get_token(), [&external_context]() noexcept {
        external_context = 42;
      });
  // input source works as expected
  input.request_stop();
  EXPECT_EQ(external_context, 42);

  ASSERT_TRUE(input.stop_requested());
  // failing to register callbacks results in noop on fuser
  ASSERT_FALSE(fuser.stop_requested());

  // disconnected fuser stops
  fuser.request_stop();
  ASSERT_TRUE(fuser.stop_requested());
}

TEST_F(FusedStopSource, OmitRegisterCallbackUpwards) {
  int external_context = 0;
  inplace_stop_source input;
  fused_stop_source<inplace_stop_token> fuser;
  auto callback =
      make_stop_callback(input.get_token(), [&external_context]() noexcept {
        external_context = 42;
      });
  // disconnected fuser stops
  fuser.request_stop();
  ASSERT_TRUE(fuser.stop_requested());
  // ... and does not impact the input
  ASSERT_FALSE(input.stop_requested());
  EXPECT_EQ(external_context, 0);

  // input source works as expected
  input.request_stop();

  EXPECT_EQ(external_context, 42);
  ASSERT_TRUE(input.stop_requested());
}

TEST_F(FusedStopSource, SingleCallback) {
  int external_context = 0;
  inplace_stop_source input;
  fused_stop_source<inplace_stop_token> fuser;
  auto callback =
      make_stop_callback(input.get_token(), [&external_context]() noexcept {
        external_context = 42;
      });
  fuser.register_callbacks(input.get_token());
  // requesting stop on input results in propagation to fuser
  input.request_stop();

  EXPECT_EQ(external_context, 42);
  ASSERT_TRUE(fuser.stop_requested());
}

TEST_F(FusedStopSource, TwoCallbacks) {
  int external_context1 = 0;
  int external_context2 = 0;
  inplace_stop_source input;
  fused_stop_source<inplace_stop_token, inplace_stop_token> fuser;
  auto callback1 =
      make_stop_callback(input.get_token(), [&external_context1]() noexcept {
        external_context1 = 42;
      });
  auto callback2 =
      make_stop_callback(input.get_token(), [&external_context2]() noexcept {
        external_context2 = 314;
      });
  fuser.register_callbacks(input.get_token(), input.get_token());
  // requesting stop on input results in propagation to fuser
  input.request_stop();

  ASSERT_TRUE(input.stop_requested());
  EXPECT_EQ(external_context1, 42);
  EXPECT_EQ(external_context2, 314);
  ASSERT_TRUE(fuser.stop_requested());
}

TEST_F(FusedStopSource, ThreeCallbacks) {
  int external_context1 = 0;
  int external_context2 = 0;
  int external_context3 = 0;
  inplace_stop_source input;
  fused_stop_source<inplace_stop_token, inplace_stop_token, inplace_stop_token>
      fuser;
  auto callback1 =
      make_stop_callback(input.get_token(), [&external_context1]() noexcept {
        external_context1 = 42;
      });
  auto callback2 =
      make_stop_callback(input.get_token(), [&external_context2]() noexcept {
        external_context2 = 314;
      });
  auto callback3 =
      make_stop_callback(input.get_token(), [&external_context3]() noexcept {
        external_context3 = 255;
      });
  fuser.register_callbacks(
      input.get_token(), input.get_token(), input.get_token());
  // requesting stop on input results in propagation to fuser
  input.request_stop();

  ASSERT_TRUE(input.stop_requested());
  EXPECT_EQ(external_context1, 42);
  EXPECT_EQ(external_context2, 314);
  EXPECT_EQ(external_context3, 255);
  ASSERT_TRUE(fuser.stop_requested());
}

TEST_F(FusedStopSource, TwoCallbacksDistinctSource) {
  int external_context1 = 0;
  int external_context2 = 0;
  inplace_stop_source input1;
  inplace_stop_source input2;
  fused_stop_source<inplace_stop_token, inplace_stop_token> fuser;
  auto callback1 =
      make_stop_callback(input1.get_token(), [&external_context1]() noexcept {
        external_context1 = 42;
      });
  auto callback2 =
      make_stop_callback(input2.get_token(), [&external_context2]() noexcept {
        external_context2 = 314;
      });
  fuser.register_callbacks(input1.get_token(), input2.get_token());
  // requesting stop on either input results in propagation to fuser
  input2.request_stop();

  ASSERT_FALSE(input1.stop_requested());
  ASSERT_TRUE(input2.stop_requested());
  EXPECT_EQ(external_context1, 0);
  EXPECT_EQ(external_context2, 314);
  ASSERT_TRUE(fuser.stop_requested());
}
