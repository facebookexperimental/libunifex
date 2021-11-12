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
#include <unifex/spawn_future.hpp>

#include <unifex/allocate.hpp>
#include <unifex/inline_scheduler.hpp>
#include <unifex/just.hpp>
#include <unifex/just_from.hpp>
#include <unifex/never.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/v1/async_scope.hpp>
#include <unifex/v2/async_scope.hpp>
#include <unifex/when_all.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

TEST(spawn_future_test, spawn_future_of_just_and_v1_scope_compiles) {
  unifex::v1::async_scope scope;

  bool didExecute = false;
  auto future = unifex::spawn_future(
      unifex::just(&didExecute) |
          unifex::then([](auto* didExecute) noexcept { *didExecute = true; }),
      scope);

  unifex::sync_wait(unifex::when_all(scope.complete(), std::move(future)));

  EXPECT_TRUE(didExecute);
}

TEST(spawn_future_test, spawn_future_of_just_and_v2_scope_compiles) {
  unifex::v2::async_scope scope;

  bool didExecute = false;
  auto future = unifex::spawn_future(
      unifex::just(&didExecute) |
          unifex::then([](auto* didExecute) noexcept { *didExecute = true; }),
      scope);

  unifex::sync_wait(unifex::when_all(scope.join(), std::move(future)));

  EXPECT_TRUE(didExecute);
}

TEST(spawn_future_test, spawn_future_increments_use_count) {
  unifex::v2::async_scope scope;

  bool lambdaHasExecuted{false};

  auto future = unifex::spawn_future(
      unifex::just_from([&]() noexcept {
        // at this point the future and the work should each hold one reference
        EXPECT_EQ(2, scope.use_count());
        lambdaHasExecuted = true;
      }),
      scope);

  EXPECT_TRUE(lambdaHasExecuted);

  unifex::sync_wait(unifex::when_all(scope.join(), std::move(future)));
}

struct identity_scope final {
  template <typename Sender>
  auto nest(Sender&& sender) noexcept(
      std::is_nothrow_constructible_v<unifex::remove_cvref_t<Sender>, Sender>) {
    return static_cast<Sender&&>(sender);
  }
};

TEST(spawn_future_test, spawn_future_accepts_non_standard_scope_types) {
  identity_scope idscope;
  auto future = unifex::spawn_future(unifex::just(42), idscope);

  auto ret = unifex::sync_wait(std::move(future));

  ASSERT_TRUE(ret.has_value());
  EXPECT_EQ(42, *ret);
}

TEST(spawn_future_test, spawn_future_is_pipeable) {
  identity_scope idscope;
  unifex::just() | unifex::spawn_future(idscope) | unifex::sync_wait();
}

TEST(spawn_future_test, spawn_future_accepts_allocators_of_non_bytes) {
  identity_scope idscope;
  unifex::spawn_future(unifex::just(), idscope, std::allocator<int>{}) |
      unifex::sync_wait();
}

TEST(
    spawn_future_test,
    spawn_future_accepts_allocators_of_non_bytes_when_piped) {
  identity_scope idscope;

  unifex::just() | unifex::spawn_future(idscope, std::allocator<int>{}) |
      unifex::sync_wait();
}

struct destruction_sensor final {
  explicit destruction_sensor(int& constructs, int& destructs) noexcept
    : constructs_(&constructs)
    , destructs_(&destructs) {
    ++(*constructs_);
  }

  destruction_sensor(const destruction_sensor& other) noexcept
    : destruction_sensor(*other.constructs_, *other.destructs_) {}

  ~destruction_sensor() { ++(*destructs_); }

private:
  int* constructs_;
  int* destructs_;
};

TEST(
    spawn_future_test,
    operation_results_are_destroyed_when_future_is_immediately_discarded) {
  unifex::v2::async_scope scope;

  int valueConstructs = 0;
  int valueDestructs = 0;
  (void)unifex::spawn_future(
      unifex::just(destruction_sensor{valueConstructs, valueDestructs}), scope);

  int errorConstructs = 0;
  int errorDestructs = 0;
  (void)unifex::spawn_future(
      // just_error is akward because future<> only supports exception_ptr
      // errors so an error of type destruction_sensor requires hoop-jumping
      unifex::just() | unifex::then([&]() {
        throw destruction_sensor{errorConstructs, errorDestructs};
      }),
      scope);

  unifex::sync_wait(scope.join());

  EXPECT_EQ(valueConstructs, valueDestructs);
  EXPECT_EQ(errorConstructs, errorDestructs);
}

TEST(spawn_future_test, happy_path_lacks_double_deletes) {
  unifex::v2::async_scope scope;

  int valueConstructs = 0;
  int valueDestructs = 0;

  unifex::spawn_future(
      unifex::just(destruction_sensor{valueConstructs, valueDestructs}),
      scope) |
      unifex::sync_wait();

  unifex::sync_wait(scope.join());

  EXPECT_EQ(valueConstructs, valueDestructs);
}

TEST(spawn_future_test, spawn_future_of_never_sender_does_not_hang) {
  unifex::v2::async_scope scope;
  auto future = unifex::spawn_future(unifex::never_sender{}, scope);

  unifex::sync_wait(unifex::when_all(
      scope.join(),
      unifex::just(std::move(future)) | unifex::then([](auto future) noexcept {
        // discard the future
        (void)future;
      })));
}

struct noop_receiver_with_scheduler {
  void set_value() noexcept {}
  void set_error(std::exception_ptr) noexcept {}
  void set_done() noexcept {};

  friend unifex::inline_scheduler tag_invoke(
      unifex::tag_t<unifex::get_scheduler>,
      noop_receiver_with_scheduler) noexcept {
    return unifex::inline_scheduler{};
  }
};

TEST(spawn_future_test, discarding_connected_future_cancels_spawned_operation) {
  unifex::v2::async_scope scope;
  auto future = unifex::spawn_future(unifex::never_sender{}, scope);

  unifex::sync_wait(unifex::when_all(
      scope.join(),
      unifex::just(std::move(future)) | unifex::then([](auto future) noexcept {
        // connect and discard the future's operation
        auto op =
            unifex::connect(std::move(future), noop_receiver_with_scheduler{});
        (void)op;
      })));
}

TEST(spawn_future_test, spawn_future_in_closed_v1_scope_is_safe) {
  unifex::v1::async_scope scope;

  unifex::sync_wait(scope.complete());

  auto ret = unifex::sync_wait(unifex::spawn_future(unifex::just(), scope));

  EXPECT_FALSE(ret.has_value());
}

TEST(spawn_future_test, spawn_future_in_closed_v2_scope_is_safe) {
  unifex::v2::async_scope scope;

  unifex::sync_wait(scope.join());

  auto ret = unifex::sync_wait(unifex::spawn_future(unifex::just(), scope));

  EXPECT_FALSE(ret.has_value());
}

#if !UNIFEX_NO_EXCEPTIONS
template <typename Scope>
struct pathological_scope final {
  size_t nestCount{};
  Scope scope;

  template <typename Sender>
  friend auto tag_invoke(
      unifex::tag_t<unifex::nest>, Sender&& sender, pathological_scope& scope) {
    if (++scope.nestCount > 1) {
      throw std::runtime_error("Too many nest calls");
    }

    return unifex::nest(static_cast<Sender&&>(sender), scope.scope);
  }
};

TEST(
    spawn_future_test,
    spawn_future_with_v1_scope_is_safe_when_first_nest_throws) {
  pathological_scope<unifex::v1::async_scope> scope;

  // burn a nest call
  scope.nestCount = 1;

  try {
    unifex::sync_wait(unifex::spawn_future(unifex::just(), scope));
    ADD_FAILURE() << "This shouldn't execute";
  } catch (const std::runtime_error& e) {
    using namespace std::literals;
    EXPECT_EQ(e.what(), "Too many nest calls"sv);
    EXPECT_EQ(2, scope.nestCount);
  } catch (...) {
    ADD_FAILURE() << "Unexpected exception";
  }

  unifex::sync_wait(scope.scope.complete());
}

TEST(
    spawn_future_test,
    spawn_future_with_v2_scope_is_safe_when_first_nest_throws) {
  pathological_scope<unifex::v2::async_scope> scope;

  // burn a nest call
  scope.nestCount = 1;

  try {
    unifex::sync_wait(unifex::spawn_future(unifex::just(), scope));
    ADD_FAILURE() << "This shouldn't execute";
  } catch (const std::runtime_error& e) {
    using namespace std::literals;
    EXPECT_EQ(e.what(), "Too many nest calls"sv);
    EXPECT_EQ(2, scope.nestCount);
  } catch (...) {
    ADD_FAILURE() << "Unexpected exception";
  }

  unifex::sync_wait(scope.scope.join());
}

TEST(spawn_future_test, spawn_future_in_open_pathological_v1_scope_is_safe) {
  pathological_scope<unifex::v1::async_scope> scope;

  try {
    unifex::sync_wait(unifex::spawn_future(unifex::just(), scope));
    ADD_FAILURE() << "This shouldn't execute";
  } catch (const std::runtime_error& e) {
    using namespace std::literals;
    EXPECT_EQ(e.what(), "Too many nest calls"sv);
    EXPECT_EQ(2, scope.nestCount);
  } catch (...) {
    ADD_FAILURE() << "Unexpected exception";
  }

  unifex::sync_wait(scope.scope.complete());
}

TEST(spawn_future_test, spawn_future_in_open_pathological_v2_scope_is_safe) {
  pathological_scope<unifex::v2::async_scope> scope;

  try {
    unifex::sync_wait(unifex::spawn_future(unifex::just(), scope));
    ADD_FAILURE() << "This shouldn't execute";
  } catch (const std::runtime_error& e) {
    using namespace std::literals;
    EXPECT_EQ(e.what(), "Too many nest calls"sv);
    EXPECT_EQ(2, scope.nestCount);
  } catch (...) {
    ADD_FAILURE() << "Unexpected exception";
  }

  unifex::sync_wait(scope.scope.join());
}

TEST(spawn_future_test, spawn_future_in_closed_pathological_v1_scope_is_safe) {
  pathological_scope<unifex::v1::async_scope> scope;

  unifex::sync_wait(scope.scope.complete());

  try {
    unifex::sync_wait(unifex::spawn_future(unifex::just(), scope));
    ADD_FAILURE() << "This shouldn't execute";
  } catch (const std::runtime_error& e) {
    using namespace std::literals;
    EXPECT_EQ(e.what(), "Too many nest calls"sv);
    EXPECT_EQ(2, scope.nestCount);
  } catch (...) {
    ADD_FAILURE() << "Unexpected exception";
  }
}

TEST(spawn_future_test, spawn_future_in_closed_pathological_v2_scope_is_safe) {
  pathological_scope<unifex::v2::async_scope> scope;

  unifex::sync_wait(scope.scope.join());

  try {
    unifex::sync_wait(unifex::spawn_future(unifex::just(), scope));
    ADD_FAILURE() << "This shouldn't execute";
  } catch (const std::runtime_error& e) {
    using namespace std::literals;
    EXPECT_EQ(e.what(), "Too many nest calls"sv);
    EXPECT_EQ(2, scope.nestCount);
  } catch (...) {
    ADD_FAILURE() << "Unexpected exception";
  }
}

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

TEST(spawn_future_test, spawn_future_maintains_the_strong_exception_guarantee) {
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
    (void)unifex::spawn_future(sender, scope, throwing_allocator<int>{});
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
    (void)unifex::spawn_future(sender, scope);
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

TEST(spawn_future_test, spawn_future_of_just_error_is_supported) {
  unifex::v2::async_scope scope;

  try {
    // it's significant that just_error() reports no invocations of set_value()
    auto fut = unifex::just_error(std::make_exception_ptr(42)) |
        unifex::spawn_future(scope);

    static_assert(unifex::same_as<
                  decltype(fut),
                  // the lack of value types on just_error() has been mapped to
                  // set_value<>()
                  unifex::v2::future<unifex::v2::async_scope>>);

    std::move(fut) | unifex::sync_wait();
  } catch (int i) {
    EXPECT_EQ(i, 42);
  } catch (...) {
    ADD_FAILURE() << "Unexpected exception.";
  }

  unifex::sync_wait(scope.join());
}
#endif

TEST(spawn_future_test, spawn_future_of_just_done_is_supported) {
  unifex::v2::async_scope scope;

  // it's significant that just_done() reports no invocations of set_value()
  auto fut = unifex::just_done() | unifex::spawn_future(scope);

  static_assert(unifex::same_as<
                decltype(fut),
                // the lack of value types on just_done() has been mapped to
                // set_value<>()
                unifex::v2::future<unifex::v2::async_scope>>);

  auto ret = std::move(fut) | unifex::sync_wait();

  EXPECT_FALSE(ret.has_value());

  unifex::sync_wait(scope.join());
}

struct pool final {
  std::atomic<size_t> allocationCount{0};
  std::atomic<size_t> deallocationCount{0};
};

template <typename T>
struct pool_allocator final {
  using value_type = T;

  constexpr explicit pool_allocator(pool& p) noexcept : pool_(&p) {}

  template <typename U, std::enable_if_t<!std::is_same_v<T, U>, int> = 0>
  constexpr pool_allocator(pool_allocator<U> other) noexcept
    : pool_(other.pool_) {}

  T* allocate(size_t n) {
    auto p = std::allocator<T>{}.allocate(n);

    pool_->allocationCount++;

    return p;
  }

  void deallocate(T* p, size_t n) noexcept {
    std::allocator<T>{}.deallocate(p, n);
    pool_->deallocationCount++;
  }

  friend constexpr bool
  operator==(pool_allocator lhs, pool_allocator rhs) noexcept {
    return lhs.pool_ == rhs.pool_;
  }

  friend constexpr bool
  operator!=(pool_allocator lhs, pool_allocator rhs) noexcept {
    return !(lhs == rhs);
  }

private:
  template <typename U>
  friend struct pool_allocator;

  pool* pool_;
};

static_assert(unifex::is_allocator_v<pool_allocator<int>>);

TEST(spawn_future_test, custom_allocator_is_propagated) {
  pool memory;
  pool_allocator<int> alloc{memory};

  unifex::v2::async_scope scope;

  {
    auto fut = unifex::spawn_future(
        unifex::sequence(
            unifex::just_from([&]() noexcept {
              // we should've allocated the spawned operation
              EXPECT_EQ(1u, memory.allocationCount);
              // but nothing should be deallocated, yet
              EXPECT_EQ(0u, memory.deallocationCount);
            }),
            unifex::allocate(unifex::just_from([&]() noexcept {
              // we should now have allocated *this* operation with the same
              // allocator
              EXPECT_EQ(2u, memory.allocationCount);
              // but still no deallocations
              EXPECT_EQ(0u, memory.deallocationCount);
            }))),
        scope,
        alloc);

    // no new allocations
    EXPECT_EQ(2u, memory.allocationCount);
    // the allocate() operation should be deallocated, but not the spawned
    // operation
    EXPECT_EQ(1u, memory.deallocationCount);
  }

  // still no new allocations
  EXPECT_EQ(2u, memory.allocationCount);
  // dropping the future should deallocate the spawned operation
  EXPECT_EQ(2u, memory.deallocationCount);

  unifex::sync_wait(scope.join());
}

TEST(spawn_future_test, futures_from_v1_scopes_are_move_assignable) {
  unifex::v1::async_scope scope;

  auto fut = unifex::spawn_future(unifex::just(0), scope);

  fut = unifex::spawn_future(unifex::just(1), scope);

  auto ret = unifex::sync_wait(std::move(fut));

  unifex::sync_wait(scope.complete());

  ASSERT_TRUE(ret.has_value());
  EXPECT_EQ(1, *ret);
}

TEST(spawn_future_test, futures_from_v2_scopes_are_move_assignable) {
  unifex::v2::async_scope scope;

  auto fut = unifex::spawn_future(unifex::just(0), scope);

  fut = unifex::spawn_future(unifex::just(1), scope);

  auto ret = unifex::sync_wait(std::move(fut));

  unifex::sync_wait(scope.join());

  ASSERT_TRUE(ret.has_value());
  EXPECT_EQ(1, *ret);
}

TEST(spawn_future_test, blocking_kind_returns_maybe) {
  // this is kind of a silly test but it confirms the relevant code compiles
  unifex::v2::async_scope scope;

  {
    auto fut = unifex::spawn_future(unifex::just(), scope);

    EXPECT_EQ(unifex::blocking_kind::maybe, unifex::blocking(fut));
  }

  unifex::sync_wait(scope.join());
}

}  // namespace
