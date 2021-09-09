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
#include <unifex/any_sender_of.hpp>
#include <unifex/just_done.hpp>
#include <unifex/let_value.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/let_value_with_stop_token.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>

#include <chrono>
#include <iostream>
#include <unifex/variant.hpp>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

TEST(LetWithStopToken, Simple) {
  timed_single_thread_context context;

  // Simple usage of 'let_value_with_stop_token()'
  // - Sets up some work to execute when receiver is cancelled
  int external_context = 0;
  optional<int> result =
      sync_wait(
          let_value_with_stop_source([&](auto& stopSource) {
            return let_value_with_stop_token([&](auto& stopToken) noexcept {
                return let_value_with([&]() noexcept {
                    auto stopCallback = [&]() noexcept { external_context = 42; };
                    using stop_token_t =
                        unifex::remove_cvref_t<decltype(stopToken)>;
                    using stop_callback_t =
                        typename stop_token_t::template callback_type<
                            decltype(stopCallback)>;
                    return stop_callback_t{stopToken, stopCallback};
                },
                [&](auto&) -> unifex::any_sender_of<int> {
                    stopSource.request_stop();
                    return just_done();
                });
            });
          })
      );

  EXPECT_TRUE(!result);
  EXPECT_EQ(external_context, 42);
}
