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

#include <unifex/single_thread_context.hpp>
#include <unifex/static_thread_pool.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/detail/completion_forwarder.hpp>

#include "failing_scheduler.hpp"

#include <gtest/gtest.h>

#include <future>

using namespace unifex;
using namespace unifex_test;

namespace {

enum class outcome { value, error, done, hop_error, hop_done };

template <typename Scheduler>
struct test_receiver {
  void set_value() noexcept { promise_->set_value(outcome::value); }
  void set_error(std::exception_ptr) noexcept {
    promise_->set_value(outcome::error);
  }
  void set_done() noexcept { promise_->set_value(outcome::done); }

  friend Scheduler
  tag_invoke(tag_t<get_scheduler>, const test_receiver& r) noexcept {
    return r.scheduler_;
  }

  Scheduler scheduler_;
  std::promise<outcome>* promise_;
};

template <typename>
inline constexpr bool dependent_false = false;

enum class hooks { none, recording, poisoned };

template <typename Scheduler, hooks Hooks>
struct test_op {
  using receiver_t = test_receiver<Scheduler>;

  explicit test_op(receiver_t receiver) noexcept : receiver_(receiver) {}

  receiver_t& get_receiver() noexcept { return receiver_; }

  void forward_set_value() noexcept { unifex::set_value(std::move(receiver_)); }

  template <typename Error>
  void forward_hop_error(Error&&) noexcept {
    static_assert(
        Hooks != hooks::poisoned || dependent_false<Error>,
        "hop error handling instantiated");
    receiver_.promise_->set_value(outcome::hop_error);
  }

  template <typename T = void>
  void forward_hop_done() noexcept {
    static_assert(
        Hooks != hooks::poisoned || dependent_false<T>,
        "hop done handling instantiated");
    receiver_.promise_->set_value(outcome::hop_done);
  }

  receiver_t receiver_;
  completion_forwarder<test_op, receiver_t, Hooks != hooks::none> forwarder_;
};

template <hooks Hooks, typename Scheduler>
outcome run(Scheduler scheduler) {
  std::promise<outcome> promise;
  test_op<Scheduler, Hooks> op{{scheduler, &promise}};
  op.forwarder_.start(op);
  return promise.get_future().get();
}

}  // namespace

#if UNIFEX_NO_ASYNC_STACKS
// Compiles only if the forwarder never instantiates hop failure handling
// for these schedulers. Async stacks instantiate set_error() for every
// receiver, so the check is only meaningful without them.
TEST(completion_forwarder_test, infallible_schedulers_skip_hop_failure_code) {
  single_thread_context thread;
  EXPECT_EQ(outcome::value, run<hooks::poisoned>(thread.get_scheduler()));

  static_thread_pool pool{1};
  EXPECT_EQ(outcome::value, run<hooks::poisoned>(pool.get_scheduler()));

  timed_single_thread_context timer;
  EXPECT_EQ(outcome::value, run<hooks::poisoned>(timer.get_scheduler()));
}
#endif

TEST(completion_forwarder_test, default_hop_error_terminates) {
  EXPECT_DEATH_IF_SUPPORTED(
      run<hooks::none>(failing_scheduler<schedule_failure::error>{}), "");
}

TEST(completion_forwarder_test, default_hop_done_forwards_done) {
  EXPECT_EQ(
      outcome::done,
      run<hooks::none>(failing_scheduler<schedule_failure::done>{}));
}

TEST(completion_forwarder_test, hook_handles_hop_error) {
  EXPECT_EQ(
      outcome::hop_error,
      run<hooks::recording>(failing_scheduler<schedule_failure::error>{}));
}

TEST(completion_forwarder_test, hook_handles_hop_done) {
  EXPECT_EQ(
      outcome::hop_done,
      run<hooks::recording>(failing_scheduler<schedule_failure::done>{}));
}
