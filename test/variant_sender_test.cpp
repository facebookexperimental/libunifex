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
#include <unifex/async_manual_reset_event.hpp>
#include <unifex/defer.hpp>
#include <unifex/dematerialize.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/let_value.hpp>
#include <unifex/materialize.hpp>
#include <unifex/nest.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/v2/async_scope.hpp>
#include <unifex/variant_sender.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {
struct IntAndStringReceiver {
    void set_value(int) noexcept {}

    void set_value(std::string) noexcept {}

    void set_done() noexcept {}

    void set_error(std::exception_ptr) noexcept {}
};

template<bool lvalueConnectNoexcept = true, bool rvalueConnectNoexcept = true>
struct TestSender {
  template <
    template <typename...> class Variant,
    template <typename...> class Tuple>
  using value_types = Variant<Tuple<std::string>>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = true;

  struct op {};

  template<typename Receiver>
  op connect([[maybe_unused]] Receiver&& r) & noexcept(lvalueConnectNoexcept) {
    return op{};
  }

  template<typename Receiver>
  op connect([[maybe_unused]] Receiver&& r) && noexcept(rvalueConnectNoexcept) {
    return op{};
  }
};
} // namespace

TEST(Variant, CombineJustAndError) {
  auto func = [](bool v) -> variant_sender<decltype(just(5)), decltype(just_error(5))> {
      if (v) {
          return just(5);
      } else {
          return just_error(10);
      }
  };

  auto just_variant_sender = func(true);
  EXPECT_FALSE(just_variant_sender.sends_done);
  auto result = sync_wait(just_variant_sender);

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 5);

  try {
      auto just_error_variant_sender = func(false);
      EXPECT_FALSE(just_error_variant_sender.sends_done);
      sync_wait(just_error_variant_sender);
      EXPECT_FALSE(true);
  } catch (int& v) {
      EXPECT_EQ(v, 10);
  }

  std::cout << "variant_sender done " << *result << "\n";
}

TEST(Variant, CombineJustAndDone) {
  auto func = [](bool v) -> variant_sender<decltype(just(5)), decltype(just_done())> {
      if (v) {
          return just(5);
      } else {
          return just_done();
      }
  };

  auto just_variant_sender = func(true);
  EXPECT_TRUE(just_variant_sender.sends_done);
  auto result = sync_wait(just_variant_sender);

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 5);

  auto just_done_variant_sender = func(false);
  EXPECT_TRUE(just_done_variant_sender.sends_done);
  auto result2 = sync_wait(just_done_variant_sender);
  EXPECT_FALSE(!!result2);

  std::cout << "variant_sender done " << *result << "\n";
}

TEST(Variant, CombineJustAndJust) {
  auto func = [&](bool v) -> variant_sender<decltype(just(5)), decltype(dematerialize(materialize(just(42))))> {
      if (v) {
          return just(5);
      } else {
          return dematerialize(materialize(just(42)));
      }
  };

  auto just_variant_sender = func(true);
  EXPECT_FALSE(just_variant_sender.sends_done);
  auto result = sync_wait(just_variant_sender);

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 5);

  auto materialized_variant_sender = func(false);
  auto result2 = sync_wait(materialized_variant_sender);
  EXPECT_TRUE(!!result2);
  EXPECT_EQ(*result2, 42);

  std::cout << "variant_sender done " << *result << "\n";
}

TEST(Variant, CombineFunctors) {
  auto f1 = [&]() noexcept -> any_sender_of<int> {
    return just(5);
  };
  auto f2 = [&]() noexcept {
    return just() | then([]() { return 42; });
  };
  using sender_type = variant_sender<decltype(f1()), decltype(f2())>;

  auto f1_sender = sender_type{f1()};
  auto result = sync_wait(std::move(f1_sender));

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 5);

  auto f2_sender = sender_type{f2()};
  auto result2 = sync_wait(std::move(f2_sender));
  EXPECT_TRUE(!!result2);
  EXPECT_EQ(*result2, 42);

  std::cout << "variant_sender done " << *result << "\n";
}

TEST(Variant, CombineJustAndJust_Invalid) {
  auto func = [](bool v) -> variant_sender<decltype(just(5)), decltype(just(std::declval<std::string>()))> {
      if (v) {
          return just(5);
      } else {
          return just(std::string("Hello World"));
      }
  };

  IntAndStringReceiver rec;

  auto just_variant_sender = func(true);
  EXPECT_FALSE(just_variant_sender.sends_done);
  auto op = unifex::connect(just_variant_sender, rec);
  unifex::start(op);

  auto just_string_sender = func(false);
  EXPECT_FALSE(just_variant_sender.sends_done);
  auto op2 = unifex::connect(just_string_sender, rec);
  unifex::start(op2);
}

namespace {
template<bool lvalueConnectNoexcept, bool rvalueConnectNoexcept>
using test_sender_t = variant_sender<TestSender<lvalueConnectNoexcept, rvalueConnectNoexcept>>;

template<typename T, bool lvalue>
using conditionally_lvalue_t = std::conditional_t<lvalue, std::add_lvalue_reference_t<T>, T>;

template<bool lvalueConnectNoexcept, bool rvalueConnectNoexcept, bool isLvalueReference = true>
using is_noexcept = unifex::is_nothrow_connectable<conditionally_lvalue_t<test_sender_t<lvalueConnectNoexcept, rvalueConnectNoexcept>, isLvalueReference>, IntAndStringReceiver>;
} // namespace

TEST(Variant, TestNoexcept) {
  auto both_no_except = is_noexcept<true, true>::value;
  EXPECT_TRUE(both_no_except);

  auto neither_no_except = is_noexcept<false, false>::value;
  EXPECT_FALSE(neither_no_except);

  auto lvalue_no_except = is_noexcept<true, false>::value;
  EXPECT_TRUE(lvalue_no_except);

  auto rvalue_no_except = is_noexcept<false, true>::value;
  EXPECT_FALSE(rvalue_no_except);
}

TEST(Variant, TestNoexcept_RvalueRef) {
  auto both_no_except = is_noexcept<true, true, false>::value;
  EXPECT_TRUE(both_no_except);

  auto neither_no_except = is_noexcept<false, false, false>::value;
  EXPECT_FALSE(neither_no_except);

  auto lvalue_no_except = is_noexcept<true, false, false>::value;
  EXPECT_FALSE(lvalue_no_except);

  auto rvalue_no_except = is_noexcept<false, true, false>::value;
  EXPECT_TRUE(rvalue_no_except);
}

TEST(Variant, TestMSVCCpp20RegressionScenario) {
  unifex::v2::async_scope scope;
  unifex::async_manual_reset_event evt{true};

  auto ret = unifex::sync_wait(unifex::nest(
      unifex::let_value(
          evt.async_wait(),
          []() noexcept {
            return unifex::variant_sender<decltype(unifex::just())>{
                unifex::just()};
          }),
      scope));

  unifex::sync_wait(scope.join());

  ASSERT_TRUE(ret.has_value());
}
