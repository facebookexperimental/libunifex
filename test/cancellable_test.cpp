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
#include <unifex/repeat_effect_until.hpp>
#include <unifex/single_thread_context.hpp>
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

TEST(cancellable_test, with_query_value_stoppable_token) {
  bool started = false;
  bool stopped = false;
  inplace_stop_source stop_src;
  auto result{sync_wait(with_query_value(
      cancellable{create_raw_sender<int>([&](auto&& receiver) {
        return test_opstate{
            std::forward<decltype(receiver)>(receiver), started, stopped};
      })},
      get_stop_token,
      stop_src.get_token()))};
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
  EXPECT_TRUE(started);
  EXPECT_FALSE(stopped);
}

TEST(cancellable_lambda_test, completes_synchronously) {
  auto result{sync_wait(cancellable{create_raw_sender<int>([](auto&& receiver) {
    return [receiver = std::forward<decltype(receiver)>(receiver)](
               auto event, auto* self) mutable {
      if constexpr (event.is_start) {
        if (try_complete(self)) {
          set_value(std::move(receiver), 42);
        }
      }
    };
  })})};
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
}

// Note: stop_while_running with stop_when is tested by the struct-based
// test above. The equivalent lambda version triggers an MSVC internal
// compiler error due to excessive template nesting depth (stop_when +
// cancellable + create_raw_sender + lambda). The stop mechanism with
// event-dispatch lambdas is covered by stops_early and
// completes_before_stop_is_forwarded below.

TEST(cancellable_lambda_test, stops_early) {
  bool started = false;
  bool stopped = false;
  auto result{sync_wait(let_value_with_stop_source([&](auto& stop_src) {
    stop_src.request_stop();
    return cancellable{
        create_raw_sender<int>([&](auto&& receiver) {
          return [receiver = std::forward<decltype(receiver)>(receiver),
                  &started,
                  &stopped](auto event, auto* self) mutable {
            if constexpr (event.is_start) {
              started = true;
              if (try_complete(self)) {
                set_value(std::move(receiver), 42);
              }
            } else if constexpr (event.is_stop) {
              stopped = true;
              if (try_complete(self)) {
                set_done(std::move(receiver));
              }
            }
          };
        }),
        std::true_type{}};
  }))};
  EXPECT_FALSE(result.has_value());
  EXPECT_FALSE(started);
  EXPECT_TRUE(stopped);
}

TEST(cancellable_lambda_test, completes_before_stop_is_forwarded) {
  bool started = false;
  bool stopped = false;
  auto result{sync_wait(let_value_with_stop_source([&](auto& stop_src) {
    stop_src.request_stop();
    return cancellable{create_raw_sender<int>([&](auto&& receiver) {
      return [receiver = std::forward<decltype(receiver)>(receiver),
              &started,
              &stopped](auto event, auto* self) mutable {
        if constexpr (event.is_start) {
          started = true;
          if (try_complete(self)) {
            set_value(std::move(receiver), 42);
          }
        } else if constexpr (event.is_stop) {
          stopped = true;
          if (try_complete(self)) {
            set_done(std::move(receiver));
          }
        }
      };
    })};
  }))};
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
  EXPECT_TRUE(started);
  EXPECT_FALSE(stopped);
}

TEST(cancellable_lambda_test, without_self_pointer) {
  // Lambda taking only (event) without self pointer.
  // Useful when try_complete is not needed (e.g. unconditional
  // synchronous completion with unstoppable token).
  auto result{sync_wait(with_query_value(
      cancellable{create_raw_sender<int>([](auto&& receiver) {
        return [receiver = std::forward<decltype(receiver)>(receiver)](
                   auto event) mutable {
          if constexpr (event.is_start) {
            set_value(std::move(receiver), 99);
          }
        };
      })},
      get_stop_token,
      unstoppable_token{}))};
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 99);
}

TEST(cancellable_lambda_test, with_non_moveable_state) {
  // Verifies that event-dispatch lambdas capturing non-moveable types
  // (std::atomic) work via aggregate init from prvalue in _event_op.
  auto result{sync_wait(with_query_value(
      cancellable{create_raw_sender<int>([](auto&& receiver) {
        return
            [receiver = std::forward<decltype(receiver)>(receiver),
             call_count = std::atomic<int>{0}](auto event, auto* self) mutable {
              if constexpr (event.is_start) {
                call_count.fetch_add(1, std::memory_order_relaxed);
                if (try_complete(self)) {
                  set_value(
                      std::move(receiver),
                      call_count.load(std::memory_order_relaxed));
                }
              }
            };
      })},
      get_stop_token,
      unstoppable_token{}))};
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 1);
}

TEST(cancellable_lambda_test, with_unstoppable_token) {
  auto result{sync_wait(with_query_value(
      cancellable{create_raw_sender<int>([](auto&& receiver) {
        return [receiver = std::forward<decltype(receiver)>(receiver)](
                   auto event, auto* self) mutable {
          if constexpr (event.is_start) {
            if (try_complete(self)) {
              set_value(std::move(receiver), 42);
            }
          }
        };
      })},
      get_stop_token,
      unstoppable_token{}))};
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
}

TEST(cancellable_lambda_test, event_probe_fields) {
  // Verify probe types have the expected boolean fields.
  // The lambda's if-constexpr dispatch depends on them.
  using namespace unifex::_lambda_op;
  static_assert(_start_probe::is_start);
  static_assert(!_start_probe::is_stop);
  static_assert(!_stop_probe::is_start);
  static_assert(_stop_probe::is_stop);
}

#  if defined(_MSC_VER)

TEST(cancellable_test, recursive_pipeline) {
  // Uses the struct-based test_sender_opstate to keep type nesting
  // shallow — the equivalent lambda version triggers an MSVC internal
  // compiler error.
  using namespace std::chrono_literals;
  async_scope scope;
  single_thread_context ctx;

  auto sender{cancellable{create_raw_sender<int>(
      [&scope](auto&& receiver)
          -> test_sender_opstate<std::remove_cvref_t<decltype(receiver)>> {
        return test_sender_opstate{
            std::forward<decltype(receiver)>(receiver), scope};
      })}};

  scope.detached_spawn_on(
      ctx.get_scheduler(),
      std::move(sender) | then([](int) noexcept {}) | repeat_effect());
  sync_wait(schedule_after(timer.get_scheduler(), 250ms));
  sync_wait(scope.cleanup());
}

#  else

TEST(cancellable_lambda_test, recursive_pipeline) {
  using namespace std::chrono_literals;
  async_scope scope;
  single_thread_context ctx;

  auto sender{cancellable{create_raw_sender<int>([&scope](auto&& receiver) {
    return [&scope, receiver = std::forward<decltype(receiver)>(receiver)](
               auto event, auto* self) mutable {
      if constexpr (event.is_start) {
        scope.detached_spawn(
            schedule_after(timer.get_scheduler(), 100ms) |
            then([self, &receiver]() noexcept {
              if (try_complete(self)) {
                set_value(std::move(receiver), 42);
              }
            }));
      } else if constexpr (event.is_stop) {
        if (try_complete(self)) {
          set_done(std::move(receiver));
        }
      }
    };
  })}};

  scope.detached_spawn_on(
      ctx.get_scheduler(),
      std::move(sender) | then([](int) noexcept {}) | repeat_effect());
  sync_wait(schedule_after(timer.get_scheduler(), 250ms));
  sync_wait(scope.cleanup());
}

#  endif  // _MSC_VER
#endif    // __cplusplus >= 201911L

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
