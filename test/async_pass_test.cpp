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

#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES

#  include <unifex/async_manual_reset_event.hpp>
#  include <unifex/async_pass.hpp>
#  include <unifex/async_scope.hpp>
#  include <unifex/let_done.hpp>
#  include <unifex/scheduler_concepts.hpp>
#  include <unifex/single_thread_context.hpp>
#  include <unifex/stop_when.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>
#  include <unifex/timed_single_thread_context.hpp>
#  include <unifex/when_all.hpp>

#  include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono_literals;
using namespace testing;

struct async_pass_test_base {
  auto stop(auto&& sender, bool& cancelled) {
    return stop_when(
        std::forward<decltype(sender)>(sender) |
            let_done([&cancelled]() noexcept {
              cancelled = true;
              return just();
            }),
        schedule_after(timer.get_scheduler(), 100ms));
  }

  async_scope scope;
  single_thread_context ctx;
  timed_single_thread_context timer;
};

template <typename AsyncPass>
class async_pass_test;

template <>
class async_pass_test<async_pass<>>
  : public Test
  , public async_pass_test_base {
public:
  async_pass<> pass;

  task<void> call(bool& completed) {
    co_await pass.async_call();
    completed = true;
  }

  task<void> throvv(bool& completed) {
    co_await pass.async_throw(std::runtime_error("throw"));
    completed = true;
  }

  task<void> throw_during_call(bool& completed) {
    co_await pass.async_call(
        [](auto&& fn) { throw std::runtime_error("throw"); });
    completed = true;
  }

  task<void> accept(bool& completed) {
    co_await pass.async_accept();
    completed = true;
  }
};

template <>
class async_pass_test<nothrow_async_pass<>>
  : public ::testing::Test
  , public async_pass_test_base {
public:
  nothrow_async_pass<> pass;

  nothrow_task<void> call(bool& completed) {
    co_await pass.async_call();
    completed = true;
  }

  nothrow_task<void> accept(bool& completed) {
    co_await pass.async_accept();
    completed = true;
  }
};

using async_pass_throw_test = async_pass_test<async_pass<>>;

TYPED_TEST_SUITE_P(async_pass_test);

TYPED_TEST_P(async_pass_test, call_before_accept) {
  bool called{false}, accepted{false};
  EXPECT_TRUE(this->pass.is_idle());
  this->scope.detached_spawn_on(this->ctx.get_scheduler(), this->call(called));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&called]() noexcept { EXPECT_FALSE(called); }));
  EXPECT_TRUE(this->pass.is_expecting_accept());
  sync_wait(this->accept(accepted));
  EXPECT_TRUE(accepted);
  this->scope.detached_spawn_call_on(
      this->ctx.get_scheduler(), [&called]() noexcept { EXPECT_TRUE(called); });
  sync_wait(this->scope.complete());
}

TYPED_TEST_P(async_pass_test, accept_before_call) {
  bool called{false}, accepted{false};
  EXPECT_TRUE(this->pass.is_idle());
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(), this->accept(accepted));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&accepted]() noexcept { EXPECT_FALSE(accepted); }));
  EXPECT_TRUE(this->pass.is_expecting_call());
  sync_wait(this->call(called));
  EXPECT_TRUE(called);
  this->scope.detached_spawn_call_on(
      this->ctx.get_scheduler(),
      [&accepted]() noexcept { EXPECT_TRUE(accepted); });
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_throw_test, throw_before_accept) {
  bool thrown{false}, accepted{false};
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(), this->throvv(thrown));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&thrown]() noexcept { EXPECT_FALSE(thrown); }));
  EXPECT_THROW(sync_wait(this->accept(accepted)), std::exception);
  EXPECT_FALSE(accepted);
  this->scope.detached_spawn_call_on(
      this->ctx.get_scheduler(), [&thrown]() noexcept { EXPECT_TRUE(thrown); });
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_throw_test, throw_during_call_before_accept) {
  bool thrown{false}, accepted{false};
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(), this->throw_during_call(thrown));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&thrown]() noexcept { EXPECT_FALSE(thrown); }));
  EXPECT_THROW(sync_wait(this->accept(accepted)), std::exception);
  EXPECT_FALSE(accepted);
  this->scope.detached_spawn_call_on(
      this->ctx.get_scheduler(), [&thrown]() noexcept { EXPECT_TRUE(thrown); });
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_throw_test, accept_before_throw) {
  bool thrown{false}, accepted{false};
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(),
      co_invoke([this, &accepted]() noexcept -> nothrow_task<void> {
        EXPECT_THROW(co_await this->accept(accepted), std::exception);
        EXPECT_FALSE(accepted);
      }));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&accepted]() noexcept { EXPECT_FALSE(accepted); }));
  sync_wait(this->throvv(thrown));
  EXPECT_TRUE(thrown);
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_throw_test, accept_before_throw_during_call) {
  bool thrown{false}, accepted{false};
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(),
      co_invoke([this, &accepted]() noexcept -> nothrow_task<void> {
        EXPECT_THROW(co_await this->accept(accepted), std::exception);
        EXPECT_FALSE(accepted);
      }));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&accepted]() noexcept { EXPECT_FALSE(accepted); }));
  sync_wait(this->throw_during_call(thrown));
  EXPECT_TRUE(thrown);
  sync_wait(this->scope.complete());
}

TYPED_TEST_P(async_pass_test, sync_accept_call) {
  bool called{false};
  auto accepted{this->pass.try_accept()};
  EXPECT_FALSE(accepted.has_value());
  this->scope.detached_spawn_on(this->ctx.get_scheduler(), this->call(called));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&called]() noexcept { EXPECT_FALSE(called); }));
  accepted = this->pass.try_accept();
  EXPECT_TRUE(accepted.has_value());
  this->scope.detached_spawn_call_on(
      this->ctx.get_scheduler(), [&called]() noexcept { EXPECT_TRUE(called); });
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_throw_test, sync_accept_throw) {
  bool thrown{false};
  auto accepted{this->pass.try_accept()};
  EXPECT_FALSE(accepted.has_value());
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(), this->throvv(thrown));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&thrown]() noexcept { EXPECT_FALSE(thrown); }));
  EXPECT_THROW(accepted = this->pass.try_accept(), std::exception);
  this->scope.detached_spawn_call_on(
      this->ctx.get_scheduler(), [&thrown]() noexcept { EXPECT_TRUE(thrown); });
  sync_wait(this->scope.complete());
}

TYPED_TEST_P(async_pass_test, sync_call) {
  bool accepted{false};
  bool called{this->pass.try_call()};
  EXPECT_FALSE(called);
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(), this->accept(accepted));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&accepted]() noexcept { EXPECT_FALSE(accepted); }));
  called = this->pass.try_call();
  EXPECT_TRUE(called);
  this->scope.detached_spawn_call_on(
      this->ctx.get_scheduler(),
      [&accepted]() noexcept { EXPECT_TRUE(accepted); });
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_throw_test, sync_accept_throw_during_call) {
  bool thrown{false};
  auto accepted{this->pass.try_accept()};
  EXPECT_FALSE(accepted.has_value());
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(), this->throw_during_call(thrown));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&thrown]() noexcept { EXPECT_FALSE(thrown); }));
  EXPECT_THROW(accepted = this->pass.try_accept(), std::exception);
  this->scope.detached_spawn_call_on(
      this->ctx.get_scheduler(), [&thrown]() noexcept { EXPECT_TRUE(thrown); });
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_throw_test, sync_throw_during_call) {
  bool accepted{false};
  bool called{this->pass.try_call()};
  EXPECT_FALSE(called);
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(),
      co_invoke([this, &accepted]() noexcept -> nothrow_task<void> {
        EXPECT_THROW(co_await this->accept(accepted), std::exception);
        EXPECT_FALSE(accepted);
        accepted = true;
      }));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&accepted]() noexcept { EXPECT_FALSE(accepted); }));
  called =
      this->pass.try_call([](auto&& fn) { throw std::runtime_error("throw"); });
  EXPECT_TRUE(called);
  this->scope.detached_spawn_call_on(
      this->ctx.get_scheduler(),
      [&accepted]() noexcept { EXPECT_TRUE(accepted); });
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_throw_test, sync_throw) {
  bool accepted{false};
  bool thrown{this->pass.try_throw(std::runtime_error("throw"))};
  EXPECT_FALSE(thrown);
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(),
      co_invoke([this, &accepted]() noexcept -> nothrow_task<void> {
        EXPECT_THROW(co_await this->accept(accepted), std::exception);
        EXPECT_FALSE(accepted);
      }));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(),
      [&accepted]() noexcept { EXPECT_FALSE(accepted); }));
  thrown = this->pass.try_throw(std::runtime_error("throw"));
  EXPECT_TRUE(thrown);
  sync_wait(this->scope.complete());
}

TYPED_TEST_P(async_pass_test, cancel_call) {
  bool called{false}, cancelled{false};
  sync_wait(this->scope.spawn_on(
      this->ctx.get_scheduler(), this->stop(this->call(called), cancelled)));
  EXPECT_FALSE(called);
  EXPECT_TRUE(cancelled);
  auto accepted{this->pass.try_accept()};
  EXPECT_FALSE(accepted.has_value());
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_throw_test, cancel_throw) {
  bool thrown{false}, cancelled{false};
  sync_wait(this->scope.spawn_on(
      this->ctx.get_scheduler(), this->stop(this->throvv(thrown), cancelled)));
  EXPECT_FALSE(thrown);
  EXPECT_TRUE(cancelled);
  auto accepted{this->pass.try_accept()};
  EXPECT_FALSE(accepted.has_value());
  sync_wait(this->scope.complete());
}

TYPED_TEST_P(async_pass_test, cancel_accept) {
  bool accepted{false}, cancelled{false};
  sync_wait(this->scope.spawn_on(
      this->ctx.get_scheduler(),
      this->stop(this->accept(accepted), cancelled)));
  EXPECT_FALSE(accepted);
  EXPECT_TRUE(cancelled);
  bool called{this->pass.try_call()};
  EXPECT_FALSE(called);
  sync_wait(this->scope.complete());
}

REGISTER_TYPED_TEST_SUITE_P(
    async_pass_test,
    call_before_accept,
    accept_before_call,
    sync_accept_call,
    sync_call,
    cancel_call,
    cancel_accept);
using async_pass_test_types = Types<async_pass<>, nothrow_async_pass<>>;
INSTANTIATE_TYPED_TEST_SUITE_P(
    async_pass_test_both, async_pass_test, async_pass_test_types);

struct Copyable {
  explicit Copyable(size_t& count) noexcept : count_(count) {}

  Copyable(const Copyable& rhs) noexcept : count_(rhs.count_) { ++count_; }

  Copyable& operator=(const Copyable& rhs) noexcept {
    std::construct_at(this, rhs);
    return *this;
  }

  size_t& count_;
};

struct Moveable {
  explicit Moveable(size_t& count) noexcept : count_(count) {}

  Moveable(Moveable&& rhs) noexcept : count_(rhs.count_) { ++count_; }

  Moveable& operator=(Moveable&& rhs) noexcept {
    std::construct_at(this, std::forward<Moveable>(rhs));
    return *this;
  }

  size_t& count_;
};

class async_pass_copy_test
  : public Test
  , public async_pass_test_base {
public:
  async_pass<const Copyable&, Moveable> pass;

  task<void> call(size_t& copies, size_t& moves) {
    co_await pass.async_call(Copyable{copies}, Moveable{moves});
  }

  task<void> accept() {
    auto [copied, moved] = co_await pass.async_accept();
    (void)copied;
    (void)moved;
    co_return;
  }
};

TEST_F(async_pass_copy_test, call_before_accept) {
  size_t copies{0}, moves{0};
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(), this->call(copies, moves));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(), [&copies, &moves]() noexcept {
        EXPECT_EQ(0, copies);
        EXPECT_EQ(0, moves);
      }));
  sync_wait(this->accept());
  // 1) explicit copy to jump the scheduler with
  // 2) internal value constructed by await_transform()
  // 3) copy of the internal value returned by await_resume()
  EXPECT_EQ(3, copies);
  EXPECT_EQ(3, moves);
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_copy_test, call_before_accept_no_coro) {
  size_t copies{0}, moves{0};
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(), this->call(copies, moves));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(), [&copies, &moves]() noexcept {
        EXPECT_EQ(0, copies);
        EXPECT_EQ(0, moves);
      }));
  sync_wait(
      pass.async_accept() | then([](const Copyable&, Moveable&&) noexcept {}));
  // 1) explicit copy to jump the scheduler with; then() receives a reference
  EXPECT_EQ(1, copies);
  EXPECT_EQ(1, moves);
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_copy_test, accept_before_call) {
  size_t copies{0}, moves{0};
  this->scope.detached_spawn_on(this->ctx.get_scheduler(), this->accept());
  sync_wait(
      this->scope.spawn_call_on(this->ctx.get_scheduler(), []() noexcept {}));
  sync_wait(this->call(copies, moves));
  sync_wait(this->scope.complete());
  // 1) explicit copy to jump the scheduler with
  // 2) internal value constructed by await_transform()
  // 3) copy of the internal value returned by await_resume()
  EXPECT_EQ(3, copies);
  EXPECT_EQ(3, moves);
}

TEST_F(async_pass_copy_test, accept_before_call_no_coro) {
  size_t copies{0}, moves{0};
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(),
      pass.async_accept() | then([](const Copyable&, Moveable&&) noexcept {}));
  sync_wait(this->call(copies, moves));
  sync_wait(this->scope.complete());
  // 1) explicit copy to jump the scheduler with; then() receives a reference
  EXPECT_EQ(1, copies);
  EXPECT_EQ(1, moves);
}

TEST_F(async_pass_copy_test, sync_accept) {
  size_t copies{0}, moves{0};
  auto accepted{this->pass.try_accept()};
  EXPECT_FALSE(accepted.has_value());
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(), this->call(copies, moves));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(), [&copies, &moves]() noexcept {
        EXPECT_EQ(0, copies);
        EXPECT_EQ(0, moves);
      }));
  accepted = this->pass.try_accept();
  EXPECT_TRUE(accepted.has_value());
  // Call sender is blocked until try_accept() completes
  // 1) result construction in try_accept()
  // 2) assignment to accepted
  EXPECT_EQ(2, copies);
  EXPECT_EQ(2, moves);
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_copy_test, sync_accept_callback) {
  size_t copies{0}, moves{0};
  auto callback{[](const Copyable&, Moveable&&) noexcept {
  }};
  auto accepted{this->pass.try_accept(callback)};
  EXPECT_FALSE(accepted);
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(), this->call(copies, moves));
  sync_wait(this->scope.spawn_call_on(
      this->ctx.get_scheduler(), [&copies, &moves]() noexcept {
        EXPECT_EQ(0, copies);
        EXPECT_EQ(0, moves);
      }));
  accepted = this->pass.try_accept(callback);
  EXPECT_TRUE(accepted);
  // No copies - call sender is blocked until callback completes
  EXPECT_EQ(0, copies);
  EXPECT_EQ(0, moves);
  sync_wait(this->scope.complete());
}

struct NonMoveable {
  NonMoveable() noexcept = default;
  NonMoveable(const NonMoveable&) = delete;
  NonMoveable(NonMoveable&&) = delete;

  int version() noexcept { return ver_.fetch_add(1); }

  std::atomic<int> ver_{0};
};

class async_pass_nocopy_test
  : public Test
  , public async_pass_test_base {
public:
  using Ref = std::reference_wrapper<NonMoveable>;
  async_pass<Ref> fwd;
  async_pass<> back;

  task<int> call() {
    NonMoveable what;
    co_await fwd.async_call(what);
    co_await back.async_accept();
    co_return what.version();
  }

  task<int> accept() {
    NonMoveable& what{(co_await fwd.async_accept()).get()};
    int version{what.version()};
    co_await back.async_call();
    co_return version;
  }
};

TEST_F(async_pass_nocopy_test, call_before_accept) {
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(),
      this->call() | then([](int version) noexcept { EXPECT_EQ(1, version); }));
  sync_wait(
      this->scope.spawn_call_on(this->ctx.get_scheduler(), []() noexcept {}));
  auto ver = sync_wait(this->accept());
  EXPECT_EQ(0, *ver);
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_nocopy_test, accept_before_call) {
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(),
      this->accept() |
          then([](int version) noexcept { EXPECT_EQ(0, version); }));
  sync_wait(
      this->scope.spawn_call_on(this->ctx.get_scheduler(), []() noexcept {}));
  auto ver = sync_wait(this->call());
  EXPECT_EQ(1, *ver);
  sync_wait(this->scope.complete());
}

struct ThrowOnCopy {
  explicit ThrowOnCopy() noexcept {}

  ThrowOnCopy(const ThrowOnCopy& rhs) {
    throw std::runtime_error("cannot copy");
  }

  ThrowOnCopy& operator=(const ThrowOnCopy& rhs) {
    std::construct_at(this, rhs);
    return *this;
  }
};

class async_pass_test_throw_on_copy
  : public Test
  , public async_pass_test_base {
public:
  async_pass<ThrowOnCopy> pass;

  task<void> call() { co_await pass.async_call(ThrowOnCopy{}); }

  task<void> accept() { co_await pass.async_accept(); }
};

TEST_F(async_pass_test_throw_on_copy, call_before_accept) {
  this->scope.detached_spawn_on(this->ctx.get_scheduler(), this->call());
  sync_wait(
      this->scope.spawn_call_on(this->ctx.get_scheduler(), []() noexcept {}));
  EXPECT_THROW(sync_wait(this->accept()), std::exception);
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_test_throw_on_copy, accept_before_call) {
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(),
      co_invoke([this]() noexcept -> nothrow_task<void> {
        EXPECT_THROW(co_await this->accept(), std::exception);
      }));
  sync_wait(
      this->scope.spawn_call_on(this->ctx.get_scheduler(), []() noexcept {}));
  sync_wait(this->call());
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_test_throw_on_copy, sync_accept_call) {
  this->scope.detached_spawn_on(this->ctx.get_scheduler(), this->call());
  sync_wait(
      this->scope.spawn_call_on(this->ctx.get_scheduler(), []() noexcept {}));
  EXPECT_THROW((void)this->pass.try_accept(), std::exception);
  sync_wait(this->scope.complete());
}

TEST_F(async_pass_test_throw_on_copy, sync_call) {
  this->scope.detached_spawn_on(
      this->ctx.get_scheduler(),
      co_invoke([this]() noexcept -> nothrow_task<void> {
        EXPECT_THROW(co_await this->accept(), std::exception);
      }));
  sync_wait(
      this->scope.spawn_call_on(this->ctx.get_scheduler(), []() noexcept {}));
  bool called{this->pass.try_call(ThrowOnCopy{})};
  EXPECT_TRUE(called);
  sync_wait(this->scope.complete());
}

#endif  // UNIFEX_NO_COROUTINES
