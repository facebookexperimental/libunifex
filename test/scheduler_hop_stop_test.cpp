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

#include <unifex/async_scope.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/v2/async_mutex.hpp>

#if __cplusplus >= 201911L
#  include <unifex/async_pass.hpp>
#  include <unifex/create_basic_sender.hpp>
#endif

#include <gtest/gtest.h>

#include <functional>
#include <future>
#include <stdexcept>

using namespace unifex;

namespace {

enum class outcome { value, error, done };

struct result {
  outcome kind;
  int value{0};
};

using ctx_scheduler =
    decltype(std::declval<single_thread_context&>().get_scheduler());

struct probe_receiver {
  void set_value() noexcept { promise_->set_value({outcome::value}); }
  void set_value(int v) noexcept { promise_->set_value({outcome::value, v}); }
  void set_error(std::exception_ptr) noexcept {
    promise_->set_value({outcome::error});
  }
  void set_done() noexcept { promise_->set_value({outcome::done}); }

  friend ctx_scheduler
  tag_invoke(tag_t<get_scheduler>, const probe_receiver& r) noexcept {
    return r.scheduler_;
  }

  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const probe_receiver& r) noexcept {
    return r.token_;
  }

  ctx_scheduler scheduler_;
  inplace_stop_token token_;
  std::promise<result>* promise_;
};

// Holds the receiver's context busy so that a scheduler hop posted by the
// operation under test stays queued while stop is requested.
struct scheduler_hop_stop_test : testing::Test {
  scheduler_hop_stop_test() {
    scope_.detached_spawn(
        schedule(ctx_.get_scheduler()) |
        then([gate{gate_.get_future().share()}]() { gate.wait(); }));
  }

  ~scheduler_hop_stop_test() { sync_wait(scope_.complete()); }

  probe_receiver receiver() noexcept {
    return probe_receiver{
        ctx_.get_scheduler(), stopSource_.get_token(), &promise_};
  }

  result stop_then_run_hop() {
    stopSource_.request_stop();
    gate_.set_value();
    return promise_.get_future().get();
  }

  single_thread_context ctx_;
  async_scope scope_;
  inplace_stop_source stopSource_;
  std::promise<void> gate_;
  std::promise<result> promise_;
};

}  // namespace

#if __cplusplus >= 201911L
TEST_F(scheduler_hop_stop_test, async_pass_acceptor_keeps_value) {
  async_pass<int> pass;
  auto op{connect(pass.async_accept(), receiver())};
  start(op);

  ASSERT_TRUE(pass.try_call(42));

  auto r{stop_then_run_hop()};
  EXPECT_EQ(outcome::value, r.kind);
  EXPECT_EQ(42, r.value);
}

TEST_F(scheduler_hop_stop_test, async_pass_caller_sees_delivery) {
  async_pass<int> pass;
  int v{42};
  auto op{connect(pass.async_call(v), receiver())};
  start(op);

  auto accepted{pass.try_accept()};
  ASSERT_TRUE(accepted.has_value());
  EXPECT_EQ(42, std::get<0>(*accepted));

  EXPECT_EQ(outcome::value, stop_then_run_hop().kind);
}

#  if !UNIFEX_NO_EXCEPTIONS
TEST_F(scheduler_hop_stop_test, async_pass_thrower_sees_delivery) {
  async_pass<int> pass;
  auto op{connect(pass.async_throw(std::runtime_error("x")), receiver())};
  start(op);

  EXPECT_THROW((void)pass.try_accept(), std::runtime_error);

  EXPECT_EQ(outcome::value, stop_then_run_hop().kind);
}
#  endif
#endif

TEST_F(scheduler_hop_stop_test, async_mutex_does_not_leak_lock) {
  v2::async_mutex mutex;
  ASSERT_TRUE(mutex.try_lock());

  auto op{connect(mutex.async_lock(), receiver())};
  start(op);
  mutex.unlock();  // hands the lock to the waiter

  auto r{stop_then_run_hop()};
  if (r.kind == outcome::value) {
    mutex.unlock();
  }
  EXPECT_EQ(outcome::value, r.kind);
  EXPECT_TRUE(mutex.try_lock()) << "lock leaked";
}

#if __cplusplus >= 201911L
TEST_F(scheduler_hop_stop_test, create_basic_sender_keeps_value) {
  std::function<void()> callback;
  auto op{connect(
      create_basic_sender<int>(
          [&callback](auto event, auto& op) {
            if constexpr (event.is_start) {
              callback = unsafe_callback<>(op);
            } else if constexpr (event.is_callback) {
              op.set_value(42);
            }
          },
          with_sender_traits<_make_traits::sender_traits_literal{
              .is_always_scheduler_affine = true}>),
      receiver())};
  start(op);

  callback();

  auto r{stop_then_run_hop()};
  EXPECT_EQ(outcome::value, r.kind);
  EXPECT_EQ(42, r.value);
}
#endif
