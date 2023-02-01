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
#include <unifex/v2/async_scope.hpp>

#include <unifex/allocate.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/just_from.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/when_all.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace {

struct async_scope_v2_test : testing::Test {
  unifex::v2::async_scope scope;

  async_scope_v2_test() = default;

  ~async_scope_v2_test() { unifex::sync_wait(scope.join()); }
};

template <typename...>
struct variant final {};

template <typename...>
struct tuple final {};

template <bool ThrowOnCopy, bool ThrowOnMove>
struct custom_receiver final {
  custom_receiver() noexcept = default;

  custom_receiver(const custom_receiver&) noexcept(!ThrowOnCopy) = default;

  custom_receiver(custom_receiver&&) noexcept(!ThrowOnMove) = default;

  ~custom_receiver() = default;

  custom_receiver&
  operator=(const custom_receiver&) noexcept(!ThrowOnCopy) = default;

  custom_receiver&
  operator=(custom_receiver&&) noexcept(!ThrowOnMove) = default;

  template <typename... T>
  void set_value(T&&...) noexcept {}

  template <typename E>
  void set_error(E&&) noexcept {}

  void set_done() noexcept {}
};

using nothrow_receiver = custom_receiver<false, false>;
using allthrow_receiver = custom_receiver<true, true>;

struct throwing_sender final {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = false;

  UNIFEX_TEMPLATE(typename Receiver)                                //
  (requires unifex::sender_to<decltype(unifex::just()), Receiver>)  //
      friend auto tag_invoke(
          unifex::tag_t<unifex::connect>,
          throwing_sender&&,
          Receiver&& receiver) noexcept(false) {
    return unifex::connect(unifex::just(), std::forward<Receiver>(receiver));
  }
};

TEST_F(async_scope_v2_test, unused_scoped_is_safe_to_join) {
  // this test does nothing beyond construct and destruct the fixture, which has
  // the side effect of constructing a scope and then joining it.

  EXPECT_EQ(0, scope.use_count());
  EXPECT_FALSE(scope.join_started());

  static_assert(
      std::is_nothrow_default_constructible_v<unifex::v2::async_scope>);
  static_assert(std::is_nothrow_destructible_v<unifex::v2::async_scope>);
  static_assert(!std::is_move_constructible_v<unifex::v2::async_scope>);
  static_assert(!std::is_copy_constructible_v<unifex::v2::async_scope>);
  static_assert(!std::is_move_assignable_v<unifex::v2::async_scope>);
  static_assert(!std::is_copy_assignable_v<unifex::v2::async_scope>);
}

TEST_F(
    async_scope_v2_test, nest_of_nullary_just_has_expected_static_properties) {
  using just_sender_t = decltype(unifex::just());

  static_assert(noexcept(scope.nest(std::declval<just_sender_t>())));
  static_assert(noexcept(scope.nest(std::declval<just_sender_t&>())));

  auto sender = scope.nest(unifex::just());

  using sender_t = decltype(sender);
  using value_types = unifex::sender_value_types_t<sender_t, variant, tuple>;
  using error_types = unifex::sender_error_types_t<sender_t, variant>;

  // values_types is just the nested sender's value_types
  static_assert(std::is_same_v<value_types, variant<tuple<>>>);
  // error_types is the nested sender's error_types
  static_assert(std::is_same_v<error_types, variant<std::exception_ptr>>);
  // sends_done is always true because the sender completes with done if nesting
  // fails
  static_assert(sender_t::sends_done);

  static_assert(std::is_nothrow_move_constructible_v<sender_t>);
  static_assert(std::is_nothrow_copy_constructible_v<sender_t>);
}

TEST_F(
    async_scope_v2_test,
    nest_of_just_of_string_has_expected_static_properties) {
  using namespace std::literals::string_literals;

  using just_sender_t = decltype(unifex::just("hello, world!"s));

  // just_sender_t has a noexcept move constructor...
  static_assert(noexcept(scope.nest(std::declval<just_sender_t>())));

  // ...but its copy constructor may throw when copying the string
  static_assert(!noexcept(scope.nest(std::declval<just_sender_t&>())));

  auto sender = scope.nest(unifex::just("hello, world!"s));

  using sender_t = decltype(sender);
  using value_types = unifex::sender_value_types_t<sender_t, variant, tuple>;
  using error_types = unifex::sender_error_types_t<sender_t, variant>;

  // values_types is just the nested sender's value_types
  static_assert(std::is_same_v<value_types, variant<tuple<std::string>>>);
  // error_types is the nested sender's error_types
  static_assert(std::is_same_v<error_types, variant<std::exception_ptr>>);
  // sends_done is always true because the sender completes with done if nesting
  // fails
  static_assert(sender_t::sends_done);

  static_assert(std::is_nothrow_move_constructible_v<sender_t>);
  static_assert(!std::is_nothrow_copy_constructible_v<sender_t>);
}

TEST_F(
    async_scope_v2_test,
    nest_of_just_error_of_int_has_expected_static_properties) {
  using just_sender_t = decltype(unifex::just_error(42));

  static_assert(noexcept(scope.nest(std::declval<just_sender_t>())));
  static_assert(noexcept(scope.nest(std::declval<just_sender_t&>())));

  auto sender = scope.nest(unifex::just_error(42));

  using sender_t = decltype(sender);
  using value_types = unifex::sender_value_types_t<sender_t, variant, tuple>;
  using error_types = unifex::sender_error_types_t<sender_t, variant>;

  // values_types is just the nested sender's value_types
  static_assert(std::is_same_v<value_types, variant<>>);
  // error_types is the nested sender's error_types + std::exception_ptr
  //
  // TODO: we don't actually need std::exception_ptr here (see TODO in
  //       v2/async_scope.hpp)
  static_assert(std::is_same_v<error_types, variant<int, std::exception_ptr>>);
  // sends_done is always true because the sender completes with done if nesting
  // fails
  static_assert(sender_t::sends_done);

  static_assert(std::is_nothrow_move_constructible_v<sender_t>);
  static_assert(std::is_nothrow_copy_constructible_v<sender_t>);
}

TEST_F(async_scope_v2_test, nest_of_just_done_has_expected_static_properties) {
  using just_sender_t = decltype(unifex::just_done());

  static_assert(noexcept(scope.nest(std::declval<just_sender_t>())));
  static_assert(noexcept(scope.nest(std::declval<just_sender_t&>())));

  auto sender = scope.nest(unifex::just_done());

  using sender_t = decltype(sender);
  using value_types = unifex::sender_value_types_t<sender_t, variant, tuple>;
  using error_types = unifex::sender_error_types_t<sender_t, variant>;

  // values_types is just the nested sender's value_types
  static_assert(std::is_same_v<value_types, variant<>>);
  // error_types is the nested sender's error_types + std::exception_ptr
  //
  // TODO: we don't actually need std::exception_ptr here (see TODO in
  //       v2/async_scope.hpp)
  static_assert(std::is_same_v<error_types, variant<std::exception_ptr>>);
  // sends_done is always true because the sender completes with done if nesting
  // fails
  static_assert(sender_t::sends_done);

  static_assert(std::is_nothrow_move_constructible_v<sender_t>);
  static_assert(std::is_nothrow_copy_constructible_v<sender_t>);
}

#if !defined(__GNUC__) || __GNUC__ > 9
// GCC9's std::tuple<> fails to find a copy constructor for just(newtype{})
TEST_F(
    async_scope_v2_test,
    nest_of_just_of_newtype_has_expected_static_properties) {
  struct newtype {
    newtype() = default;
    newtype(const newtype&) noexcept(false) = default;
    newtype(newtype&&) noexcept(false) = default;
    ~newtype() = default;
  };

  using just_sender_t = decltype(unifex::just(newtype{}));

// MSVC incorrectly fails these assertions
#  ifndef _MSC_VER
  // just_sender_t has a throwing move constructor...
  static_assert(!noexcept(scope.nest(std::declval<just_sender_t>())));

  // ...and a throwing copy constructor
  static_assert(!noexcept(scope.nest(std::declval<just_sender_t&>())));
#  endif
  auto sender = scope.nest(unifex::just(newtype{}));

  using sender_t = decltype(sender);
  using value_types = unifex::sender_value_types_t<sender_t, variant, tuple>;
  using error_types = unifex::sender_error_types_t<sender_t, variant>;

  // values_types is just the nested sender's value_types
  static_assert(std::is_same_v<value_types, variant<tuple<newtype>>>);
  // error_types is the nested sender's error_types
  static_assert(std::is_same_v<error_types, variant<std::exception_ptr>>);
  // sends_done is always true because the sender completes with done if nesting
  // fails
  static_assert(sender_t::sends_done);
// MSVC incorrectly fails these assertions
#  ifndef _MSC_VER
  static_assert(!std::is_nothrow_move_constructible_v<sender_t>);
  static_assert(!std::is_nothrow_copy_constructible_v<sender_t>);
#  endif
}
#endif

TEST_F(
    async_scope_v2_test,
    connect_of_nest_sender_has_expected_static_properties) {
  using sender_t = decltype(scope.nest(unifex::just()));

  static_assert(unifex::is_nothrow_connectable_v<sender_t, nothrow_receiver>);
  static_assert(unifex::is_nothrow_connectable_v<sender_t, nothrow_receiver&>);

  static_assert(unifex::is_nothrow_connectable_v<sender_t&, nothrow_receiver>);
  static_assert(unifex::is_nothrow_connectable_v<sender_t&, nothrow_receiver&>);

  static_assert(!unifex::is_nothrow_connectable_v<sender_t, allthrow_receiver>);
  static_assert(
      !unifex::is_nothrow_connectable_v<sender_t, allthrow_receiver&>);

  static_assert(
      !unifex::is_nothrow_connectable_v<sender_t&, allthrow_receiver>);
  static_assert(
      !unifex::is_nothrow_connectable_v<sender_t&, allthrow_receiver&>);

  using throwing_sender_t = decltype(scope.nest(throwing_sender{}));

  static_assert(
      !unifex::is_nothrow_connectable_v<throwing_sender_t, nothrow_receiver>);
  static_assert(
      !unifex::is_nothrow_connectable_v<throwing_sender_t, nothrow_receiver&>);

  static_assert(
      !unifex::is_nothrow_connectable_v<throwing_sender_t&, nothrow_receiver>);
  static_assert(
      !unifex::is_nothrow_connectable_v<throwing_sender_t&, nothrow_receiver&>);
}

TEST_F(async_scope_v2_test, nest_owns_one_refcount) {
  EXPECT_EQ(0, scope.use_count());

  {
    auto sender = scope.nest(unifex::just());

    EXPECT_EQ(1, scope.use_count());
  }

  EXPECT_EQ(0, scope.use_count());
}

TEST_F(
    async_scope_v2_test, nest_sender_move_constructor_transfers_its_reference) {
  EXPECT_EQ(0, scope.use_count());

  auto sender = scope.nest(unifex::just());

  EXPECT_EQ(1, scope.use_count());

  auto sender2 = std::move(sender);

  EXPECT_EQ(1, scope.use_count());
}

TEST_F(
    async_scope_v2_test,
    nest_sender_copy_constructor_increments_refcount_when_scope_is_open) {
  EXPECT_EQ(0, scope.use_count());

  auto sender = scope.nest(unifex::just());

  EXPECT_EQ(1, scope.use_count());

  auto sender2 = sender;

  EXPECT_EQ(2, scope.use_count());
}

TEST_F(
    async_scope_v2_test,
    nest_sender_copy_constructor_produces_ready_done_sender_when_scope_is_closed) {
  EXPECT_EQ(0, scope.use_count());

  std::optional sender = scope.nest(unifex::just());

  EXPECT_EQ(1, scope.use_count());

  unifex::sync_wait(unifex::when_all(
      scope.join(), unifex::just_from([&sender, this]() noexcept {
        EXPECT_TRUE(scope.join_started());

        EXPECT_EQ(1, scope.use_count());

        auto sender2 = *sender;

        EXPECT_EQ(1, scope.use_count());

        auto ret = unifex::sync_wait(std::move(sender2));

        EXPECT_FALSE(ret.has_value());

        sender.reset();

        EXPECT_EQ(0, scope.use_count());
      })));
}

TEST_F(
    async_scope_v2_test,
    connect_of_rvalue_nest_sender_transfers_reference_to_nest_op) {
  EXPECT_EQ(0, scope.use_count());

  auto sender = scope.nest(unifex::just());

  EXPECT_EQ(1, scope.use_count());

  {
    auto op = unifex::connect(std::move(sender), nothrow_receiver{});

    EXPECT_EQ(1, scope.use_count());
  }

  EXPECT_EQ(0, scope.use_count());
}

TEST_F(
    async_scope_v2_test,
    connect_of_lvalue_nest_sender_increments_refcount_when_scope_is_open) {
  EXPECT_EQ(0, scope.use_count());

  auto sender = scope.nest(unifex::just());

  EXPECT_EQ(1, scope.use_count());

  {
    auto op = unifex::connect(sender, nothrow_receiver{});

    EXPECT_EQ(2, scope.use_count());
  }

  EXPECT_EQ(1, scope.use_count());
}

TEST_F(
    async_scope_v2_test,
    connect_of_lvalue_nest_sender_leaves_refcount_unchanged_when_scope_is_closed) {
  EXPECT_EQ(0, scope.use_count());

  std::optional sender = scope.nest(unifex::just());

  EXPECT_EQ(1, scope.use_count());

  unifex::sync_wait(unifex::when_all(
      scope.join(), unifex::just_from([&sender, this]() noexcept {
        EXPECT_TRUE(scope.join_started());

        EXPECT_EQ(1, scope.use_count());

        auto op = unifex::connect(*sender, nothrow_receiver{});

        EXPECT_EQ(1, scope.use_count());

        sender.reset();

        EXPECT_EQ(0, scope.use_count());
      })));
}

TEST_F(
    async_scope_v2_test,
    running_nest_sender_passes_through_wrapped_sender_behaviour) {
  auto optInt = unifex::sync_wait(scope.nest(unifex::just(42)));

  ASSERT_TRUE(optInt.has_value());
  EXPECT_EQ(42, *optInt);

  // TODO: factor this in terms of let_error so we can check this logic even
  //       when exceptions are disabled
  try {
    // allocate the nested sender to help catch lifetime bugs with ASAN
    unifex::sync_wait(scope.nest(unifex::allocate(unifex::just_error(42))));
  } catch (int i) {
    EXPECT_EQ(42, i);
  } catch (...) {
    EXPECT_FALSE(true) << "Shouldn't have caught anything but an int";
  }

  auto emptyOpt = unifex::sync_wait(scope.nest(unifex::just_done()));

  EXPECT_FALSE(emptyOpt.has_value());
}

TEST_F(
    async_scope_v2_test,
    running_nest_senders_constructed_before_joining_sees_normal_completion_after_join_starts) {
  auto intSender = scope.nest(unifex::just(42));
  auto errorSender = scope.nest(unifex::just_error(42));
  auto doneSender = scope.nest(unifex::just_done());

  unifex::sync_wait(unifex::when_all(
      scope.join(), unifex::just_from([&, this]() noexcept {
        EXPECT_TRUE(scope.join_started());

        auto optInt = unifex::sync_wait(std::move(intSender));

        ASSERT_TRUE(optInt.has_value());
        EXPECT_EQ(42, *optInt);

        // TODO: factor this in terms of let_error so we can check this logic
        //       even when exceptions are disabled
        try {
          unifex::sync_wait(std::move(errorSender));
        } catch (int i) {
          EXPECT_EQ(42, i);
        } catch (...) {
          EXPECT_FALSE(true) << "Shouldn't have caught anything but an int";
        }

        auto emptyOpt = unifex::sync_wait(std::move(doneSender));

        EXPECT_FALSE(emptyOpt.has_value());
      })));
}

TEST_F(
    async_scope_v2_test,
    starting_a_nest_sender_after_the_scope_has_ended_produces_done) {
  unifex::sync_wait(unifex::when_all(
      scope.join(), unifex::just_from([&, this]() noexcept {
        EXPECT_TRUE(scope.join_started());

        auto emptyOpt = unifex::sync_wait(scope.nest(unifex::just(42)));

        EXPECT_FALSE(emptyOpt.has_value());
      })));
}

}  // namespace
