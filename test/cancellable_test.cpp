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
#include <unifex/cancellable.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/with_query_value.hpp>

#if __cplusplus >= 201911L
#  include <unifex/create_raw_sender.hpp>
#endif

#include <gtest/gtest.h>

#include <atomic>
#include <memory>

namespace {

using namespace unifex;
using namespace testing;

timed_single_thread_context timer;

template <typename Receiver>
struct test_sender_opstate {
  struct shared {
    std::atomic<test_sender_opstate*> self{nullptr};
  };

  explicit test_sender_opstate(Receiver&& receiver, async_scope& scope)
    : receiver_(std::forward<Receiver>(receiver))
    , scope_(scope) {}

  void start() noexcept {
    using namespace std::chrono_literals;
    shared_->self.store(this, std::memory_order_release);
    scope_.detached_spawn(
        timer.get_scheduler().schedule_after(500ms) |
        then([s = shared_]() noexcept {
          if (auto* self =
                  s->self.exchange(nullptr, std::memory_order_acq_rel)) {
            if (try_complete(self)) {
              set_value(std::move(self->receiver_), 42);
            }
          }
        }));
  }

  void stop() noexcept {
    if (shared_->self.exchange(nullptr, std::memory_order_acq_rel)) {
      if (try_complete(this)) {
        set_done(std::move(receiver_));
      }
    }
  }

  Receiver receiver_;
  async_scope& scope_;
  std::shared_ptr<shared> shared_ = std::make_shared<shared>();
};

template <typename Receiver>
struct test_opstate {
  explicit test_opstate(Receiver&& receiver, bool& started, bool& stopped)
    : receiver_(std::forward<Receiver>(receiver))
    , started_(started)
    , stopped_(stopped) {}

  void start() noexcept {
    started_ = true;
    if (try_complete(this)) {
      set_value(std::move(receiver_), 42);
    }
  }

  void stop() noexcept {
    stopped_ = true;
    if (try_complete(this)) {
      set_done(std::move(receiver_));
    }
  }

  Receiver receiver_;
  bool& started_;
  bool& stopped_;
};

struct test_inplace_sender {
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<int>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  explicit test_inplace_sender(bool& started, bool& stopped)
    : started_(started)
    , stopped_(stopped) {}

  template <typename Receiver>
  friend auto tag_invoke(
      tag_t<connect>,
      test_inplace_sender&& self,
      Receiver&& receiver) noexcept {
    return test_opstate{
        std::forward<Receiver>(receiver), self.started_, self.stopped_};
  }

  bool& started_;
  bool& stopped_;
};
#if __cplusplus >= 201911L

TEST(cancellable_test, stop_while_running) {
  using namespace std::chrono_literals;
  async_scope scope;
  auto result{sync_wait(stop_when(
      cancellable{create_raw_sender<int>([&scope](auto&& receiver) {
        return test_sender_opstate{
            std::forward<decltype(receiver)>(receiver), scope};
      })},
      timer.get_scheduler().schedule_after(100ms)))};
  EXPECT_FALSE(result.has_value());
  sync_wait(scope.complete());
}

TEST(cancellable_test, stops_early) {
  bool started = false;
  bool stopped = false;
  auto result{sync_wait(let_value_with_stop_source([&](auto& stop_src) {
    stop_src.request_stop();
    return cancellable{
        create_raw_sender<int>([&](auto&& receiver) {
          return test_opstate{
              std::forward<decltype(receiver)>(receiver), started, stopped};
        }),
        std::true_type{}};
  }))};
  EXPECT_FALSE(result.has_value());
  EXPECT_FALSE(started);
  EXPECT_TRUE(stopped);
}

TEST(cancellable_test, completes_before_stop_is_forwarded) {
  bool started = false;
  bool stopped = false;
  auto result{sync_wait(let_value_with_stop_source([&](auto& stop_src) {
    stop_src.request_stop();
    return cancellable{create_raw_sender<int>([&](auto&& receiver) {
      return test_opstate{
          std::forward<decltype(receiver)>(receiver), started, stopped};
    })};
  }))};
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
  EXPECT_TRUE(started);
  EXPECT_FALSE(stopped);
}

TEST(cancellable_test, stops_after_start) {
  using namespace std::chrono_literals;
  async_scope scope;
  auto result{sync_wait(let_value_with_stop_source([&](auto& stop_src) {
    stop_src.request_stop();
    return cancellable{create_raw_sender<int>([&scope](auto&& receiver) {
      return test_sender_opstate{
          std::forward<decltype(receiver)>(receiver), scope};
    })};
  }))};
  EXPECT_FALSE(result.has_value());
  sync_wait(scope.complete());
}

TEST(cancellable_test, completes_with_unstoppable_token) {
  bool started = false;
  bool stopped = false;
  auto result{sync_wait(with_query_value(
      cancellable{create_raw_sender<int>([&](auto&& receiver) {
        return test_opstate{
            std::forward<decltype(receiver)>(receiver), started, stopped};
      })},
      get_stop_token,
      unstoppable_token{}))};
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
  EXPECT_TRUE(started);
  EXPECT_FALSE(stopped);
}

#endif

TEST(cancellable_test, constructs_sender_in_place) {
  bool started = false;
  bool stopped = false;
  auto result{sync_wait(with_query_value(
      cancellable<test_inplace_sender>{started, stopped},
      get_stop_token,
      unstoppable_token{}))};
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
  EXPECT_TRUE(started);
  EXPECT_FALSE(stopped);
}

}  // namespace
