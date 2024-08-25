/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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
#include <unifex/spawn_detached.hpp>

#include <unifex/just.hpp>
#include <unifex/just_from.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/v1/async_scope.hpp>
#include <unifex/v2/async_scope.hpp>

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

namespace {

TEST(spawn_detached_test, spawn_detached_of_just_and_v1_scope_compiles) {
  unifex::v1::async_scope scope;

  bool didExecute = false;
  unifex::spawn_detached(
      unifex::just(&didExecute) |
          unifex::then([](auto* didExecute) noexcept { *didExecute = true; }),
      scope);

  unifex::sync_wait(scope.complete());

  EXPECT_TRUE(didExecute);
}

TEST(spawn_detached_test, spawn_detached_of_just_and_v2_scope_compiles) {
  unifex::v2::async_scope scope;

  bool didExecute = false;
  unifex::spawn_detached(
      unifex::just(&didExecute) |
          unifex::then([](auto* didExecute) noexcept { *didExecute = true; }),
      scope);

  unifex::sync_wait(scope.join());

  EXPECT_TRUE(didExecute);
}

TEST(spawn_detached_test, spawn_detached_increments_use_count) {
  unifex::v2::async_scope scope;

  bool lambdaHasExecuted{false};

  unifex::spawn_detached(
      unifex::just_from([&]() noexcept {
        EXPECT_EQ(1, scope.use_count());
        lambdaHasExecuted = true;
      }),
      scope);

  EXPECT_TRUE(lambdaHasExecuted);

  unifex::sync_wait(scope.join());
}

struct identity_scope final {
  template <typename Sender>
  auto nest(Sender&& sender) noexcept(
      std::is_nothrow_constructible_v<unifex::remove_cvref_t<Sender>, Sender>) {
    return static_cast<Sender&&>(sender);
  }
};

TEST(spawn_detached_test, spawn_detached_accepts_non_standard_scope_types) {
  identity_scope idscope;
  unifex::spawn_detached(unifex::just(), idscope);
}

TEST(spawn_detached_test, spawn_detached_is_pipeable) {
  identity_scope idscope;
  unifex::just() | unifex::spawn_detached(idscope);
}

TEST(spawn_detached_test, spawn_detached_accepts_allocators_of_non_bytes) {
  identity_scope idscope;
  unifex::spawn_detached(unifex::just(), idscope, std::allocator<int>{});
}

#if !UNIFEX_NO_EXCEPTIONS
template <typename T>
struct throwing_allocator final {
  using value_type = T;

  constexpr throwing_allocator() noexcept = default;

  constexpr throwing_allocator(const throwing_allocator&) noexcept = default;

  template <typename U>
  constexpr throwing_allocator(const throwing_allocator<U>&) noexcept {}

  ~throwing_allocator() = default;

  constexpr throwing_allocator& operator=(throwing_allocator) noexcept {
    return *this;
  }

  [[noreturn]] T* allocate(size_t) { throw std::bad_alloc{}; }

  [[noreturn]] void deallocate(void*, size_t) noexcept {
    // allocate never succeeds so calling deallocate is a bug
    std::terminate();
  }

  static constexpr bool always_equal = true;

  constexpr friend bool
  operator==(throwing_allocator, throwing_allocator) noexcept {
    return true;
  }

  constexpr friend bool
  operator!=(throwing_allocator, throwing_allocator) noexcept {
    return false;
  }
};

static_assert(unifex::is_allocator_v<throwing_allocator<int>>);

TEST(
    spawn_detached_test,
    spawn_detached_maintains_the_strong_exception_guarantee) {
  unifex::v2::async_scope scope;

  bool connected = false;
  bool started = false;

  auto sender = unifex::let_value_with(
      [&]() -> int {
        // this state factory runs upon connect
        connected = true;
        throw 42;
      },
      [&](auto&) noexcept {
        // this successor factory doesn't run until start
        started = true;
        return unifex::just();
      });

  try {
    unifex::spawn_detached(sender, scope, throwing_allocator<int>{});
  } catch (std::bad_alloc&) {
    // expected
  } catch (std::exception& e) {
    ADD_FAILURE() << "Unexpected exception: " << e.what();
  } catch (...) {
    ADD_FAILURE() << "Unexpected exception.";
  }

  EXPECT_FALSE(connected);
  EXPECT_FALSE(started);

  try {
    unifex::spawn_detached(sender, scope);
  } catch (int i) {
    EXPECT_EQ(42, i);
  } catch (std::exception& e) {
    ADD_FAILURE() << "Unexpected exception: " << e.what();
  } catch (...) {
    ADD_FAILURE() << "Unexpected exception.";
  }

  EXPECT_TRUE(connected);
  EXPECT_FALSE(started);

  unifex::sync_wait(scope.join());
}
#endif

struct read_async_stack_frame {
  template <
      template <typename...>
      typename Variant,
      template <typename...>
      typename Tuple>
  using value_types = Variant<Tuple<unifex::AsyncStackFrame*>>;

  template <template <typename...> typename Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = false;

  static constexpr unifex::blocking_kind blocking =
      unifex::blocking_kind::always_inline;

  template <typename Receiver>
  struct op {
    Receiver receiver;

    void start() & noexcept {
      auto frame = unifex::get_async_stack_frame(receiver);
      unifex::set_value(std::move(receiver), frame);
    }
  };

  template <typename Receiver>
  using op_t = op<unifex::remove_cvref_t<Receiver>>;

  template <typename Receiver>
  op_t<Receiver> connect(Receiver&& receiver) const noexcept {
    return {std::forward<Receiver>(receiver)};
  }

  friend unifex::instruction_ptr tag_invoke(
      unifex::tag_t<unifex::get_return_address>,
      const read_async_stack_frame& self) noexcept {
    return self.returnAddress;
  }

  unifex::instruction_ptr returnAddress;
};

TEST(spawn_detached_test, capstone_receiver_has_expected_async_stack_depth) {
  identity_scope scope;

  // this is a meaningless but unique address that we can check for in our test
  auto returnAddress = unifex::instruction_ptr::read_return_address();

  unifex::spawn_detached(
      unifex::then(
          read_async_stack_frame{returnAddress},
          [&](auto* frame) noexcept {
            if constexpr (!UNIFEX_NO_ASYNC_STACKS) {
              // the expected structure of this operation is:
              // op = connect(then-sender, capstone-receiver)
              //   child = connect(read-sender, then-receiver)
              //
              // There's no nest-sender because we're using an identity_scope,
              // which implements nest() by returning its argument.
              //
              // Each connect() wraps the resulting operation in a stack
              // frame-injecting operation state/receiver pair so we expect the
              // read-sender to get a non-null frame from its wrapper-receiver;
              // the parent of that frame should come from the then-sender's
              // wrapper-receiver; the parent of that frame should come from the
              // spawn_detached capstone receiver.

              // the read-sender's frame
              ASSERT_NE(frame, nullptr);
              EXPECT_EQ(frame->getReturnAddress(), returnAddress);

              // the then-sender's frame
              ASSERT_NE(frame->getParentFrame(), nullptr);

              // the capstone receiver's frame
              ASSERT_NE(frame->getParentFrame()->getParentFrame(), nullptr);

              // there should be no further frames
              EXPECT_EQ(
                  frame->getParentFrame()->getParentFrame()->getParentFrame(),
                  nullptr);
            } else {
              EXPECT_EQ(frame, nullptr);
            }
          }),
      scope);
}

}  // namespace
