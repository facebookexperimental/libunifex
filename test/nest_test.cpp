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
#include <unifex/nest.hpp>

#include <unifex/just.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/v1/async_scope.hpp>
#include <unifex/v2/async_scope.hpp>

#include <gtest/gtest.h>

namespace {

struct tag_invocable_scope {
  template <typename Sender>
  friend auto tag_invoke(
      unifex::tag_t<unifex::nest>,
      Sender&& sender,
      tag_invocable_scope& scope) noexcept {
    scope.invoked = true;
    return static_cast<Sender&&>(sender);
  }

  bool invoked{false};
};

struct member_invocable_scope {
  template <typename Sender>
  auto nest(Sender&& sender) noexcept {
    invoked = true;
    return static_cast<Sender&&>(sender);
  }

  bool invoked{false};
};

struct scope_with_member_and_tag_invoke final {
  template <typename Sender>
  friend auto tag_invoke(
      unifex::tag_t<unifex::nest>,
      Sender&& sender,
      scope_with_member_and_tag_invoke& scope) noexcept {
    scope.tagInvokeInvoked = true;
    return static_cast<Sender&&>(sender);
  }

  template <typename Sender>
  auto nest(Sender&& sender) noexcept {
    memberNestInvoked = true;
    return static_cast<Sender&&>(sender);
  }

  bool tagInvokeInvoked{false};
  bool memberNestInvoked{false};
};

TEST(nest_test, nest_of_tag_invocable_scope_invokes_tag_invoke) {
  tag_invocable_scope scope;

  ASSERT_FALSE(scope.invoked);

  unifex::sync_wait(unifex::nest(unifex::just(), scope));

  EXPECT_TRUE(scope.invoked);
}

TEST(nest_test, nest_of_member_invocable_scope_invokes_member) {
  member_invocable_scope scope;

  ASSERT_FALSE(scope.invoked);

  unifex::sync_wait(unifex::nest(unifex::just(), scope));

  EXPECT_TRUE(scope.invoked);
}

TEST(nest_test, nest_is_pipeable) {
  tag_invocable_scope tscope;
  member_invocable_scope mscope;

  unifex::sync_wait(unifex::just() | unifex::nest(tscope));
  unifex::sync_wait(unifex::just() | unifex::nest(mscope));

  EXPECT_TRUE(tscope.invoked);
  EXPECT_TRUE(mscope.invoked);
}

TEST(nest_test, nest_of_v2_scope_invokes_member) {
  unifex::v2::async_scope scope;

  {
    auto cpoSender = unifex::nest(unifex::just(), scope);
    auto memberSender = scope.nest(unifex::just());

    static_assert(unifex::same_as<decltype(cpoSender), decltype(memberSender)>);
  }

  unifex::sync_wait(scope.join());
}

TEST(nest_test, nest_of_v1_scope_invokes_member) {
  unifex::v1::async_scope scope;

  {
    auto cpoSender = unifex::nest(unifex::just(), scope);
    auto memberSender = scope.attach(unifex::just());

    static_assert(unifex::same_as<decltype(cpoSender), decltype(memberSender)>);
  }

  unifex::sync_wait(scope.complete());
}

TEST(nest_test, tag_invoke_is_preferred_over_member_nest) {
  scope_with_member_and_tag_invoke scope;

  unifex::sync_wait(unifex::nest(unifex::just(), scope));

  EXPECT_TRUE(scope.tagInvokeInvoked);
  EXPECT_FALSE(scope.memberNestInvoked);
}

// gcc 9 fails to compute all the noexcept assertions in
// nest_has_the_expected_noexcept_clause
#if !defined(__GNUC__) || __GNUC__ > 9
struct throwing_sender final {
  template <
      template <typename...>
      typename Variant,
      template <typename...>
      typename Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> typename Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = false;

  throwing_sender() noexcept(false) = default;

  throwing_sender(const throwing_sender&) noexcept(false) = default;

  ~throwing_sender() = default;

  template <typename Receiver>
  friend auto tag_invoke(
      unifex::tag_t<unifex::connect>, throwing_sender, Receiver&& r) noexcept
      -> decltype(unifex::connect(unifex::just(), static_cast<Receiver&&>(r))) {
    return unifex::connect(unifex::just(), static_cast<Receiver&&>(r));
  }
};

static_assert(unifex::typed_sender<throwing_sender>);

TEST(nest_test, nest_has_the_expected_noexcept_clause) {
  tag_invocable_scope tscope;
  member_invocable_scope mscope;

  unifex::v2::async_scope v2scope;

  static_assert(noexcept(unifex::nest(unifex::just(), tscope)));
  static_assert(noexcept(unifex::nest(unifex::just(), mscope)));
  static_assert(noexcept(unifex::nest(unifex::just(), v2scope)));

  static_assert(noexcept(unifex::nest(tscope)));
  static_assert(noexcept(unifex::nest(mscope)));
  static_assert(noexcept(unifex::nest(v2scope)));

  static_assert(noexcept(unifex::just() | unifex::nest(tscope)));
  static_assert(noexcept(unifex::just() | unifex::nest(mscope)));
  static_assert(noexcept(unifex::just() | unifex::nest(v2scope)));

  // the noexcept clause should adjust to the underlying scope's clause;
  // v2::async_scope's noexcept clause should be false when nesting a
  // throwing_sender
  static_assert(!noexcept(unifex::nest(throwing_sender{}, v2scope)));
  static_assert(!noexcept(throwing_sender{} | unifex::nest(v2scope)));

  unifex::sync_wait(v2scope.join());
}
#endif  // !defined(__GNUC__) || __GNUC__ > 9

TEST(nest_test, nest_operation_drops_scope_reference_on_completion) {
  struct receiver {
    void set_value() noexcept {}
    void set_error(std::exception_ptr) noexcept {}
    void set_done() noexcept {}
  };

  unifex::v2::async_scope scope;

  {
    auto op = unifex::connect(unifex::nest(unifex::just(), scope), receiver{});

    EXPECT_EQ(scope.use_count(), 1);

    unifex::start(op);

    // the operation is fully synchronous so it's done by now
    EXPECT_EQ(scope.use_count(), 0);
  }

  unifex::sync_wait(scope.join());
}

}  // namespace
