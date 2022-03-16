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
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>
#include <unifex/utility.hpp>
#include <unifex/variant.hpp>

#include <chrono>
#include <iostream>
#include <string>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {
struct my_error {};
}

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

#endif // !UNIFEX_NO_EXCEPTIONS

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
