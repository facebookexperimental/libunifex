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

#include <unifex/variant.hpp>
#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

struct UnstoppableSimpleIntReceiver {
  void set_value(int) noexcept {}

  void set_error(std::exception_ptr) noexcept {}

  void set_done() noexcept {}
};

struct InplaceStoppableIntReceiver : public UnstoppableSimpleIntReceiver {
  InplaceStoppableIntReceiver(inplace_stop_source& source) noexcept
    : source_(source) {}

  friend inplace_stop_token tag_invoke(
      tag_t<get_stop_token>, const InplaceStoppableIntReceiver& r) noexcept {
    return r.source_.get_token();
  }

  inplace_stop_source& source_;
};

struct inplace_stop_token_redux : public inplace_stop_token {
  inplace_stop_token_redux(inplace_stop_token token)
    : inplace_stop_token(token) {}
};

struct NonInplaceStoppableIntReceiver : public UnstoppableSimpleIntReceiver {
  NonInplaceStoppableIntReceiver(inplace_stop_source& source) noexcept
    : source_(source) {}

  friend inplace_stop_token_redux tag_invoke(
      tag_t<get_stop_token>, const NonInplaceStoppableIntReceiver& r) noexcept {
    return inplace_stop_token_redux{r.source_.get_token()};
  }

  inplace_stop_source& source_;
};

TEST(LetWithStopToken, Simple) {
  timed_single_thread_context context;

  // Simple usage of 'let_value_with_stop_token()'
  // - Sets up some work to execute when receiver is cancelled
  int external_context = 0;
  optional<int> result =
      sync_wait(let_value_with_stop_source([&](auto& stopSource) {
        return let_value_with_stop_token([&](auto& stopToken) {
          return let_value_with(
              [&]() noexcept {
                auto stopCallback = [&]() noexcept {
                  external_context = 42;
                };
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
      }));

  EXPECT_TRUE(!result);
  EXPECT_EQ(external_context, 42);
}

TEST(LetWithStopToken, SimpleNoExceptSuccessor) {
  // Simple usage of 'let_value_with_stop_token()'
  // - Sets up some work to execute when receiver is cancelled
  int external_context = 0;
  optional<int> result =
      sync_wait(let_value_with_stop_source([&](auto& stopSource) noexcept {
        return let_value_with_stop_token([&](auto& stopToken) noexcept {
          return let_value_with(
              [&]() noexcept {
                auto stopCallback = [&]() noexcept {
                  external_context = 42;
                };
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
      }));

  EXPECT_TRUE(!result);
  EXPECT_EQ(external_context, 42);
}

TEST(LetWithStopToken, SimpleInplaceStoppableNoexcept) {
  static_assert(std::is_same_v<
                stop_token_type_t<InplaceStoppableIntReceiver>,
                inplace_stop_token>);

  // Simple usage of 'let_value_with_stop_token()' with receiver with inplace
  // stop token
  // - Sets up some work to execute when receiver is cancelled
  int external_context = 0;
  inplace_stop_source stopSource;
  auto stop_token_functor = [&](auto stopToken) noexcept {
    return let_value_with(
        [stopToken, &external_context]() noexcept {
          auto stopCallback = [&]() noexcept {
            external_context = 42;
          };
          using stop_token_t = unifex::remove_cvref_t<decltype(stopToken)>;
          using stop_callback_t = typename stop_token_t::template callback_type<
              decltype(stopCallback)>;
          return stop_callback_t{stopToken, stopCallback};
        },
        [&](auto&) noexcept {
          stopSource.request_stop();
          return just_done();
        });
  };
  static_assert(is_nothrow_connectable_v<
                callable_result_t<
                    decltype(let_value_with_stop_token),
                    decltype(stop_token_functor)>,
                InplaceStoppableIntReceiver>);
  auto op = unifex::connect(
      let_value_with_stop_token(std::move(stop_token_functor)),
      InplaceStoppableIntReceiver{stopSource});
  unifex::start(op);

  EXPECT_EQ(external_context, 42);
}

TEST(LetWithStopToken, SimpleUnstoppable) {
  static_assert(is_stop_never_possible_v<
                stop_token_type_t<UnstoppableSimpleIntReceiver>>);

  // Simple usage of 'let_value_with_stop_token()' with receiver with
  // unstoppable stop token
  // - Sets up some work to execute when receiver is cancelled
  // - Work is never completed since token is unstoppable
  int external_context = 0;
  inplace_stop_source stopSource;
  auto op = unifex::connect(
      let_value_with_stop_token([&](auto stopToken) noexcept {
        return let_value_with(
            [stopToken, &external_context]() noexcept {
              auto stopCallback = [&]() noexcept {
                external_context = 42;
              };
              using stop_token_t = unifex::remove_cvref_t<decltype(stopToken)>;
              using stop_callback_t =
                  typename stop_token_t::template callback_type<
                      decltype(stopCallback)>;
              return stop_callback_t{stopToken, stopCallback};
            },
            [&](auto&) -> unifex::any_sender_of<int> { return just_done(); });
      }),
      UnstoppableSimpleIntReceiver{});
  unifex::start(op);

  EXPECT_EQ(external_context, 0);
}

TEST(LetWithStopToken, SimpleInplaceStoppable) {
  static_assert(std::is_same_v<
                stop_token_type_t<InplaceStoppableIntReceiver>,
                inplace_stop_token>);

  // Simple usage of 'let_value_with_stop_token()' with receiver with inplace
  // stop token
  // - Sets up some work to execute when receiver is cancelled
  int external_context = 0;
  inplace_stop_source stopSource;
  auto op = unifex::connect(
      let_value_with_stop_token([&](auto stopToken) noexcept {
        return let_value_with(
            [stopToken, &external_context]() noexcept {
              auto stopCallback = [&]() noexcept {
                external_context = 42;
              };
              using stop_token_t = unifex::remove_cvref_t<decltype(stopToken)>;
              using stop_callback_t =
                  typename stop_token_t::template callback_type<
                      decltype(stopCallback)>;
              return stop_callback_t{stopToken, stopCallback};
            },
            [&](auto&) -> unifex::any_sender_of<int> {
              stopSource.request_stop();
              return just_done();
            });
      }),
      InplaceStoppableIntReceiver{stopSource});
  unifex::start(op);

  EXPECT_EQ(external_context, 42);
}

TEST(LetWithStopToken, SimpleNonInplaceStoppable) {
  static_assert(!std::is_same_v<
                stop_token_type_t<NonInplaceStoppableIntReceiver>,
                inplace_stop_token>);

  // Simple usage of 'let_value_with_stop_token()' with receiver with stop token
  // that's stoppable but not inplace stop token
  // - Sets up some work to execute when receiver is cancelled
  int external_context = 0;
  inplace_stop_source stopSource;
  auto op = unifex::connect(
      let_value_with_stop_token([&](auto stopToken) noexcept {
        return let_value_with(
            [stopToken, &external_context]() noexcept {
              auto stopCallback = [&]() noexcept {
                external_context = 42;
              };
              using stop_token_t = unifex::remove_cvref_t<decltype(stopToken)>;
              using stop_callback_t =
                  typename stop_token_t::template callback_type<
                      decltype(stopCallback)>;
              return stop_callback_t{stopToken, stopCallback};
            },
            [&](auto&) -> unifex::any_sender_of<int> {
              stopSource.request_stop();
              return just_done();
            });
      }),
      NonInplaceStoppableIntReceiver{stopSource});
  unifex::start(op);

  EXPECT_EQ(external_context, 42);
}