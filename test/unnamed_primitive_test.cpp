/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unifex/unnamed_primitive.hpp>

#include <unifex/inline_scheduler.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/transform_done.hpp>
#include <unifex/with_query_value.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>

using testing::Invoke;
using testing::_;
using unifex::unnamed_primitive;
using unifex::connect;
using unifex::get_scheduler;
using unifex::get_stop_token;
using unifex::inline_scheduler;
using unifex::inplace_stop_source;
using unifex::inplace_stop_token;
using unifex::just;
using unifex::schedule;
using unifex::single_thread_context;
using unifex::start;
using unifex::sync_wait;
using unifex::tag_t;
using unifex::transform;
using unifex::transform_done;
using unifex::with_query_value;

namespace {

struct mock_receiver_impl {
  MOCK_METHOD(void, set_value, (), ());
  MOCK_METHOD(void, set_error, (std::exception_ptr), (noexcept));
  MOCK_METHOD(void, set_done, (), (noexcept));
};

// mock_receiver_impl cannot be used directly as a receiver because the MOCK
// macros make the type non-movable, non-copyable. Receivers must be movable.
struct mock_receiver {
  mock_receiver(inline_scheduler& scheduler, inplace_stop_source& stopSource)
      : impl(std::make_unique<mock_receiver_impl>()),
        scheduler(&scheduler),
        stopToken(stopSource.get_token()) {}

  void set_value() {
    impl->set_value();
  }

  void set_error(std::exception_ptr e) noexcept {
    impl->set_error(e);
  }

  void set_done() noexcept {
    impl->set_done();
  }

  std::unique_ptr<mock_receiver_impl> impl;
  inline_scheduler* scheduler;
  inplace_stop_token stopToken;

  friend inline_scheduler tag_invoke(
      tag_t<get_scheduler>, const mock_receiver& self) noexcept {
    return *self.scheduler;
  }

  friend inplace_stop_token tag_invoke(
      tag_t<get_stop_token>, const mock_receiver& self) noexcept {
    return self.stopToken;
  }
};

} // namespace

struct unnamed_primitive_test : testing::Test {
  inplace_stop_source stopSource;
  inline_scheduler scheduler;
  mock_receiver receiver{scheduler, stopSource};
  mock_receiver_impl& receiverImpl = *receiver.impl;
};

TEST_F(unnamed_primitive_test, default_constructor_leaves_primitive_unready) {
  unnamed_primitive evt;

  EXPECT_FALSE(evt.ready());
}

TEST_F(unnamed_primitive_test, can_construct_initially_ready_primitive) {
  unnamed_primitive evt{true};

  EXPECT_TRUE(evt.ready());
}

TEST_F(unnamed_primitive_test, set_makes_unready_primitive_ready) {
  unnamed_primitive evt;

  evt.set();

  EXPECT_TRUE(evt.ready());
}

TEST_F(unnamed_primitive_test, reset_makes_ready_primitive_unready) {
  unnamed_primitive evt{true};

  evt.reset();

  EXPECT_FALSE(evt.ready());
}

TEST_F(unnamed_primitive_test, sender_completes_after_set_when_connected_to_unready_primitive) {
  unnamed_primitive evt;

  auto op = connect(evt.async_wait(), std::move(receiver));

  {
    EXPECT_CALL(receiverImpl, set_value()).Times(0);
    EXPECT_CALL(receiverImpl, set_error(_)).Times(0);
    EXPECT_CALL(receiverImpl, set_done()).Times(0);

    start(op);
  }

  EXPECT_CALL(receiverImpl, set_value()).Times(1);
  EXPECT_CALL(receiverImpl, set_error(_)).Times(0);
  EXPECT_CALL(receiverImpl, set_done()).Times(0);

  evt.set();
}

TEST_F(unnamed_primitive_test, sender_connected_to_unready_primitive_can_be_cancelled) {
  unnamed_primitive evt;

  auto op = connect(evt.async_wait(), std::move(receiver));

  {
    EXPECT_CALL(receiverImpl, set_value()).Times(0);
    EXPECT_CALL(receiverImpl, set_error(_)).Times(0);
    EXPECT_CALL(receiverImpl, set_done()).Times(0);

    start(op);
  }

  EXPECT_CALL(receiverImpl, set_value()).Times(0);
  EXPECT_CALL(receiverImpl, set_error(_)).Times(0);
  EXPECT_CALL(receiverImpl, set_done()).Times(1);

  stopSource.request_stop();
}

TEST_F(unnamed_primitive_test, sender_cancels_immediately_if_stopped_before_start) {
  unnamed_primitive evt;

  auto op = connect(evt.async_wait(), std::move(receiver));

  stopSource.request_stop();

  EXPECT_CALL(receiverImpl, set_value()).Times(0);
  EXPECT_CALL(receiverImpl, set_error(_)).Times(0);
  EXPECT_CALL(receiverImpl, set_done()).Times(1);

  start(op);
}

TEST_F(
    unnamed_primitive_test,
    sender_connected_to_ready_primitive_cancels_immediately_if_stopped_before_start) {

  unnamed_primitive evt{true};

  auto op = connect(evt.async_wait(), std::move(receiver));

  stopSource.request_stop();

  EXPECT_CALL(receiverImpl, set_value()).Times(0);
  EXPECT_CALL(receiverImpl, set_error(_)).Times(0);
  EXPECT_CALL(receiverImpl, set_done()).Times(1);

  start(op);
}

TEST_F(unnamed_primitive_test, sender_completes_inline_when_connected_to_ready_primitive) {
  unnamed_primitive evt{true};

  auto op = connect(evt.async_wait(), std::move(receiver));

  EXPECT_CALL(receiverImpl, set_value()).Times(1);
  EXPECT_CALL(receiverImpl, set_error(_)).Times(0);
  EXPECT_CALL(receiverImpl, set_done()).Times(0);

  start(op);
}

TEST_F(unnamed_primitive_test, exception_from_set_value_sent_to_set_error) {
  unnamed_primitive evt{true};

  auto op = connect(evt.async_wait(), std::move(receiver));

  EXPECT_CALL(receiverImpl, set_value())
      .WillOnce(Invoke([]() -> void {
        throw std::runtime_error("from set_value()");
      }));

  EXPECT_CALL(receiverImpl, set_error(_))
      .WillOnce(Invoke([](std::exception_ptr eptr) noexcept {
        try {
          std::rethrow_exception(eptr);
        } catch (const std::runtime_error& e) {
          EXPECT_STREQ(e.what(), "from set_value()");
        }
      }));

  start(op);
}

template <typename Scheduler>
static std::thread::id getThreadId(Scheduler& scheduler) {
  return sync_wait(transform(schedule(scheduler), [] {
    return std::this_thread::get_id();
  })).value();
}

TEST_F(
    unnamed_primitive_test,
    set_value_reschedules_when_invoked_from_async_wait) {

  single_thread_context thread;
  auto scheduler = thread.get_scheduler();

  const auto expectedThreadId = getThreadId(scheduler);

  ASSERT_NE(expectedThreadId, std::this_thread::get_id());

  unnamed_primitive evt{true};

  auto actualThreadId = sync_wait(transform(
      with_query_value(evt.async_wait(), get_scheduler, scheduler),
      [] { return std::this_thread::get_id(); })).value();

  EXPECT_EQ(expectedThreadId, actualThreadId);
}

TEST_F(
    unnamed_primitive_test,
    set_value_reschedules_when_invoked_from_set) {

  single_thread_context thread;
  auto scheduler = thread.get_scheduler();

  const auto expectedThreadId = getThreadId(scheduler);

  ASSERT_NE(expectedThreadId, std::this_thread::get_id());

  unnamed_primitive evt1, evt2;

  auto op = connect(
      with_query_value(evt1.async_wait(), get_scheduler, scheduler),
      std::move(receiver));

  start(op);

  std::thread::id actualThreadId{};

  EXPECT_CALL(receiverImpl, set_value())
      .WillOnce(Invoke([&actualThreadId, &evt2] {
        actualThreadId = std::this_thread::get_id();
        evt2.set();
      }));

  evt1.set();

  sync_wait(evt2.async_wait());

  EXPECT_EQ(expectedThreadId, actualThreadId);
}

TEST_F(
    unnamed_primitive_test,
    cancellation_is_rescheduled_when_starting_after_stopping) {

  single_thread_context thread;
  auto scheduler = thread.get_scheduler();

  const auto expectedThreadId = getThreadId(scheduler);

  ASSERT_NE(expectedThreadId, std::this_thread::get_id());

  unnamed_primitive evt1, evt2;

  auto op = connect(
      with_query_value(evt1.async_wait(), get_scheduler, scheduler),
      std::move(receiver));

  stopSource.request_stop();

  std::thread::id actualThreadId{};

  EXPECT_CALL(receiverImpl, set_value()).Times(0);
  EXPECT_CALL(receiverImpl, set_error(_)).Times(0);

  EXPECT_CALL(receiverImpl, set_done())
      .WillOnce(Invoke([&actualThreadId, &evt2] {
        actualThreadId = std::this_thread::get_id();
        evt2.set();
      }));

  ON_CALL(receiverImpl, set_value())
      .WillByDefault(Invoke([&evt2] {
        // this is an error condition, but let's not hang the test
        evt2.set();
      }));

  start(op);

  sync_wait(evt2.async_wait());

  EXPECT_EQ(expectedThreadId, actualThreadId);
}

TEST_F(
    unnamed_primitive_test,
    cancellation_is_rescheduled_when_stopping_after_starting) {

  single_thread_context thread;
  auto scheduler = thread.get_scheduler();

  const auto expectedThreadId = getThreadId(scheduler);

  ASSERT_NE(expectedThreadId, std::this_thread::get_id());

  unnamed_primitive evt1, evt2;

  auto op = connect(
      with_query_value(evt1.async_wait(), get_scheduler, scheduler),
      std::move(receiver));

  start(op);

  std::thread::id actualThreadId{};

  EXPECT_CALL(receiverImpl, set_value()).Times(0);
  EXPECT_CALL(receiverImpl, set_error(_)).Times(0);

  EXPECT_CALL(receiverImpl, set_done())
      .WillOnce(Invoke([&actualThreadId, &evt2] {
        actualThreadId = std::this_thread::get_id();
        evt2.set();
      }));

  ON_CALL(receiverImpl, set_value())
      .WillByDefault(Invoke([&evt2] {
        // this is an error condition, but let's not hang the test
        evt2.set();
      }));

  stopSource.request_stop();

  sync_wait(evt2.async_wait());

  EXPECT_EQ(expectedThreadId, actualThreadId);
}
