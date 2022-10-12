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

}  // namespace
