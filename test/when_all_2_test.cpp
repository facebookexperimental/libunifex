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
#include <unifex/allocate.hpp>
#include <unifex/finally.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/just_from.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/utility.hpp>
#include <unifex/variant.hpp>
#include <unifex/when_all.hpp>

#include <chrono>
#include <iostream>
#include <string>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {
struct my_error {};

template <typename StopToken, typename Callback>
auto make_stop_callback(StopToken stoken, Callback callback) {
  using stop_callback_t = typename StopToken::template callback_type<Callback>;

  return stop_callback_t{stoken, std::move(callback)};
}

struct _cancel_only_sender {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<>>;
  template <template <typename...> class Variant>
  using error_types = Variant<>;
  static constexpr bool sends_done = true;

  template <typename Receiver>
  struct operation;

  template <typename Receiver>
  struct cancel_operation {
    operation<Receiver>& op_;

    void operator()() noexcept { op_.set_done(); }
  };

  template <typename Receiver>
  struct operation {
    using receiver_t = unifex::remove_cvref_t<Receiver>;
    using receiver_stop_token_t = stop_token_type_t<Receiver&>;

    receiver_t receiver_;
    receiver_stop_token_t stoken_ = get_stop_token(receiver_);
    cancel_operation<receiver_t> cancel_{*this};
    decltype(make_stop_callback(stoken_, cancel_)) callback_ =
        make_stop_callback(stoken_, cancel_);

    void start() noexcept {}

    void set_done() noexcept { unifex::set_done(std::move(receiver_)); }
  };

  template <typename Receiver>
  auto connect(Receiver&& receiver) const& noexcept {
    return operation<Receiver>{static_cast<Receiver&&>(receiver)};
  }
};

inline const struct _fn {
  template <typename... Values>
  constexpr auto operator()(Values&&...) const noexcept {
    return _cancel_only_sender{};
  }
} cancel_only_sender{};

}  // namespace

#if !UNIFEX_NO_EXCEPTIONS

TEST(WhenAll2, Smoke) {
  timed_single_thread_context context;

  auto scheduler = context.get_scheduler();

  auto startTime = steady_clock::now();

  bool ranPart1Callback = false;
  bool ranPart2Callback = false;
  bool ranFinalCallback = false;

  try {
    sync_wait(then(
        when_all(
            then(
                schedule_after(scheduler, 100ms),
                [&]() -> steady_clock::time_point::duration {
                  ranPart1Callback = true;
                  auto time = steady_clock::now() - startTime;
                  auto timeMs = duration_cast<milliseconds>(time).count();
                  std::cout << "part1 finished - [" << timeMs
                            << "ms] throwing\n";
                  throw my_error{};
                }),
            then(
                schedule_after(scheduler, 200ms),
                [&]() {
                  ranPart2Callback = true;
                  auto time = steady_clock::now() - startTime;
                  auto timeMs = duration_cast<milliseconds>(time).count();
                  std::cout << "part2 finished - [" << timeMs << "ms]\n";
                  return time;
                })),
        [&](auto&& a, auto&& b) {
          ranFinalCallback = true;
          std::cout << "when_all finished - ["
                    << duration_cast<milliseconds>(std::get<0>(var::get<0>(a)))
                           .count()
                    << "ms, "
                    << duration_cast<milliseconds>(std::get<0>(var::get<0>(b)))
                           .count()
                    << "ms]\n";
        }));
    FAIL();
  } catch (my_error) {
    auto time = steady_clock::now() - startTime;
    auto timeMs = duration_cast<milliseconds>(time).count();
    std::cout << "caught my_error after " << timeMs << "ms\n";
  }

  EXPECT_TRUE(ranPart1Callback);
  EXPECT_FALSE(ranPart2Callback);
  EXPECT_FALSE(ranFinalCallback);
}

#endif  // !UNIFEX_NO_EXCEPTIONS

struct string_const_ref_sender {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<const std::string&>>;

  template <template <typename...> class Variant>
  using error_types = Variant<const std::exception_ptr&>;

  static constexpr bool sends_done = false;

  template <typename Receiver>
  struct operation {
    unifex::remove_cvref_t<Receiver> receiver_;
    void start() & noexcept {
      std::string s = "hello world";
      unifex::set_value(std::move(receiver_), std::as_const(s));
      s = "goodbye old value";
    }
  };

  template <typename Receiver>
  operation<Receiver> connect(Receiver&& r) const& {
    return operation<Receiver>{(Receiver &&) r};
  }
};

TEST(WhenAll2, ResultsAreDecayCopied) {
  optional<std::tuple<
      variant<std::tuple<std::string>>,
      variant<std::tuple<std::string>>>>
      result = sync_wait(
          when_all(string_const_ref_sender{}, string_const_ref_sender{}));
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(
      "hello world", std::get<0>(var::get<0>(std::get<0>(result.value()))));
  EXPECT_EQ(
      "hello world", std::get<0>(var::get<0>(std::get<1>(result.value()))));
}

TEST(WhenAll2, SenderIsLvalueConnectable) {
  auto test = unifex::when_all(unifex::just(), unifex::just());

  unifex::sync_wait(test);
}

TEST(WhenAll2, ErrorCancelsRest) {
  try {
    sync_wait(when_all(
        // arm #1: use allocate() to trigger ASAN
        finally(allocate(when_all(cancel_only_sender())), just()),
        // arm #2: immediately throw to trigger cancellation of arm #1
        just_from([]() { throw 1; })));
  } catch (...) {
  }
}
