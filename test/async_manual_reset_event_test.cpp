/*
 * Copyright 2019-present Facebook, Inc.
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

#include <unifex/async_manual_reset_event.hpp>

#include <unifex/inline_scheduler.hpp>
#include <unifex/sender_concepts.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <exception>
#include <memory>
#include <stdexcept>

using testing::Invoke;
using testing::_;
using unifex::async_manual_reset_event;
using unifex::connect;
using unifex::get_scheduler;
using unifex::inline_scheduler;
using unifex::start;

namespace {

struct mock_receiver_impl {
  MOCK_METHOD(void, set_value, (), ());
  MOCK_METHOD(void, set_error, (std::exception_ptr), (noexcept));
};

// mock_receiver_impl cannot be used directly as a receiver because the MOCK
// macros make the type non-movable, non-copyable. Receivers must be movable.
struct mock_receiver {
  mock_receiver(inline_scheduler& scheduler)
      : impl(std::make_unique<mock_receiver_impl>()), scheduler(&scheduler) {}

  void set_value() {
    impl->set_value();
  }

  void set_error(std::exception_ptr e) noexcept {
    impl->set_error(e);
  }

  void set_done() noexcept {
    std::terminate();
  }

  std::unique_ptr<mock_receiver_impl> impl;
  inline_scheduler* scheduler;

  friend inline_scheduler tag_invoke(
      unifex::tag_t<get_scheduler>,
      const mock_receiver& self) noexcept {
    return *self.scheduler;
  }
};

} // namespace

struct async_manual_reset_event_test : testing::Test {
  inline_scheduler scheduler;
  mock_receiver receiver{scheduler};
  mock_receiver_impl& receiverImpl = *receiver.impl;
};

TEST_F(async_manual_reset_event_test, default_constructor_leaves_baton_unready) {
  async_manual_reset_event evt;

  EXPECT_FALSE(evt.ready());
}

TEST_F(async_manual_reset_event_test, can_construct_initially_ready_baton) {
  async_manual_reset_event evt{true};

  EXPECT_TRUE(evt.ready());
}

TEST_F(async_manual_reset_event_test, set_makes_unready_baton_ready) {
  async_manual_reset_event evt;

  evt.set();

  EXPECT_TRUE(evt.ready());
}

TEST_F(async_manual_reset_event_test, sender_completes_after_set_when_connected_to_unready_baton) {
  async_manual_reset_event evt;

  auto op = connect(evt.async_wait(), std::move(receiver));

  {
    EXPECT_CALL(receiverImpl, set_value()).Times(0);
    EXPECT_CALL(receiverImpl, set_error(_)).Times(0);

    start(op);
  }

  EXPECT_CALL(receiverImpl, set_value()).Times(1);
  EXPECT_CALL(receiverImpl, set_error(_)).Times(0);

  evt.set();
}

TEST_F(async_manual_reset_event_test, sender_completes_inline_when_connected_to_ready_baton) {
  async_manual_reset_event evt{true};

  auto op = connect(evt.async_wait(), std::move(receiver));

  EXPECT_CALL(receiverImpl, set_value()).Times(1);
  EXPECT_CALL(receiverImpl, set_error(_)).Times(0);

  start(op);
}

TEST_F(async_manual_reset_event_test, exception_from_set_value_sent_to_set_error) {
  async_manual_reset_event evt{true};

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
