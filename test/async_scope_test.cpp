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

#include <unifex/allocate.hpp>
#include <unifex/any_sender_of.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_from.hpp>
#include <unifex/just_void_or_done.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/let_value_with_stop_token.hpp>
#include <unifex/never.hpp>
#include <unifex/on.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sequence.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/async_scope.hpp>
#include <unifex/when_all.hpp>

#include "mock_receiver.hpp"
#include "stoppable_receiver.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <optional>

using namespace unifex;
using namespace unifex_test;

namespace {

struct signal_on_destruction {
  async_manual_reset_event* destroyed_;

  signal_on_destruction(async_manual_reset_event* destroyed) noexcept
    : destroyed_(destroyed) {}

  signal_on_destruction(signal_on_destruction&& other) noexcept
    : destroyed_(std::exchange(other.destroyed_, nullptr)) {}

  ~signal_on_destruction() {
    if (destroyed_) {
      destroyed_->set();
    }
  }
};

}  // namespace

struct async_scope_test : testing::Test {
  async_scope scope;
  single_thread_context thread;

  async_scope_test() = default;

  ~async_scope_test() { sync_wait(scope.cleanup()); }

  void spawn_work_after_cleanup() {
    sync_wait(scope.cleanup());

    async_manual_reset_event destroyed;
    bool executed = false;

    scope.detached_spawn_on(
        thread.get_scheduler(),
        let_value_with(
            [&, tmp = signal_on_destruction{&destroyed}]() noexcept {
              executed = true;
              return 42;
            },
            [&](auto&) noexcept {
              return just_from([&]() noexcept { executed = true; });
            }));

    sync_wait(destroyed.async_wait());

    EXPECT_FALSE(executed);
  }

  void expect_work_to_run() {
    future<int, int> fut =
        scope.spawn_on(thread.get_scheduler(), just(42, 42));

    // we'll hang here if the above work doesn't start
    auto result = sync_wait(std::move(fut));

    ASSERT_TRUE(result);
    EXPECT_EQ(std::tuple(42, 42), *result);
  }

  void expect_work_to_run_call_on() {
    async_manual_reset_event evt;

    future<> fut = scope.spawn_call_on(
        thread.get_scheduler(), [&]() noexcept { evt.set(); });

    // we'll hang here if the above work doesn't start
    sync_wait(evt.async_wait());
    sync_wait(std::move(fut));
  }
};

TEST_F(async_scope_test, spawning_nullary_just_signals_future) {
  auto fut = scope.spawn(just());

  static_assert(same_as<decltype(fut), future<>>);

  auto result = sync_wait(std::move(fut));

  EXPECT_TRUE(result);
}

TEST_F(async_scope_test, spawning_just_with_an_int_signals_future) {
  auto fut = scope.spawn(just(42));

  static_assert(same_as<decltype(fut), future<int>>);

  auto result = sync_wait(std::move(fut));

  ASSERT_TRUE(result);
  EXPECT_EQ(*result, 42);
}

TEST_F(async_scope_test, spawning_just_with_a_triple_of_ints_signals_future) {
  auto fut = scope.spawn(just(42, 43, 44));

  static_assert(same_as<decltype(fut), future<int, int, int>>);

  auto result = sync_wait(std::move(fut));

  ASSERT_TRUE(result);
  EXPECT_EQ(*result, std::tuple(42, 43, 44));
}

TEST_F(
    async_scope_test, spawning_just_void_or_done_signals_the_future_with_done) {
  auto fut = scope.spawn(just_void_or_done(false));

  static_assert(same_as<decltype(fut), future<>>);

  auto result = sync_wait(std::move(fut));

  EXPECT_FALSE(result);
}

TEST_F(
    async_scope_test,
    spawning_just_from_throwing_function_signals_the_future_with_an_exception) {
  auto fut = scope.spawn(just_from([]() { throw 1; }));

  static_assert(same_as<decltype(fut), future<>>);

  try {
    sync_wait(std::move(fut));
    FAIL();
  } catch (int i) {
    EXPECT_EQ(i, 1);
  } catch (...) {
    FAIL();
  }
}

namespace {

template <typename StopToken, typename Callback>
auto make_stop_callback(StopToken stoken, Callback callback) {
  using stop_callback_t = typename StopToken::template callback_type<Callback>;

  return stop_callback_t{stoken, std::move(callback)};
}

}  // namespace

TEST_F(async_scope_test, discarding_a_future_requests_cancellation) {
  async_manual_reset_event scheduled, finished;

  std::atomic<bool> wasStopped{false};

  std::optional<future<>> optFuture = scope.spawn_on(
      thread.get_scheduler(),
      let_value_with_stop_token([&](auto stoken) noexcept {
        return let_value_with(
            [&wasStopped, stoken]() mutable noexcept {
              return make_stop_callback(
                  stoken, [&wasStopped]() noexcept { wasStopped = true; });
            },
            [&scheduled, &finished](auto&) noexcept {
              return sequence(
                  just_from([&scheduled]() noexcept { scheduled.set(); }),
                  finished.async_wait());
            });
      }));

  // ensure the spawned work is actually spawned before...
  sync_wait(scheduled.async_wait());

  // ...dropping the future
  optFuture.reset();

  // we know that the stop callback has been registered (that happens before the
  // spawned work sets the scheduled event) so dropping the future ought to
  // trigger the callback and set wasStopped to true
  EXPECT_TRUE(wasStopped.load());

  // now clean up the test state; release the awaited event and block until the
  // scope sees the work finish (skipping this last step causes a race between
  // waking up the blocked work and destroying finished)

  finished.set();

  sync_wait(scope.complete());
}

TEST_F(async_scope_test, requesting_the_scope_stop_cancels_pending_futures) {
  async_manual_reset_event evt;

  auto fut = scope.spawn_on(thread.get_scheduler(), evt.async_wait());

  scope.request_stop();

  // with the scope cancelled, pending futures should all immediately complete
  // with done
  auto result = sync_wait(std::move(fut));

  EXPECT_FALSE(result);

  // but the scope itself won't complete until the spawned work is actually done
  // so we need to release the event here and block on scope completion before
  // the event is destroyed to make sure the test actually completes
  evt.set();

  sync_wait(scope.complete());
}

TEST_F(async_scope_test, spawning_after_cleaning_up_destroys_the_sender) {
  spawn_work_after_cleanup();
}

TEST_F(async_scope_test, cleanup_is_idempotent) {
  sync_wait(scope.cleanup());

  spawn_work_after_cleanup();
}

TEST_F(async_scope_test, spawning_work_makes_it_run) {
  expect_work_to_run();

  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, spawning_work_makes_it_run_with_lambda) {
  expect_work_to_run_call_on();

  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, scope_not_stopped_until_cleanup_is_started) {
  auto cleanup = scope.cleanup();

  expect_work_to_run();

  sync_wait(std::move(cleanup));
}

TEST_F(async_scope_test, work_spawned_in_correct_context) {
  auto futureId = scope.spawn_on(thread.get_scheduler(), just_from([] {
                                   return std::this_thread::get_id();
                                 }));
  auto id = sync_wait(std::move(futureId));
  sync_wait(scope.cleanup());
  ASSERT_TRUE(id);
  EXPECT_EQ(*id, thread.get_thread_id());
  EXPECT_NE(*id, std::this_thread::get_id());
}

TEST_F(async_scope_test, lots_of_threads_works) {
  constexpr int maxCount = 1'000;
  std::array<single_thread_context, maxCount> threads;

  async_manual_reset_event evt1, evt2, evt3;
  std::atomic<int> count{0};

  struct decr {
    decr(std::atomic<int>& count, async_manual_reset_event& evt) noexcept
      : count_(&count)
      , evt_(&evt) {}

    decr(decr&& other) = delete;

    ~decr() {
      UNIFEX_ASSERT(evt_->ready());
      count_->fetch_sub(1, std::memory_order_relaxed);
    }

    std::atomic<int>* count_;
    async_manual_reset_event* evt_;
  };

  for (auto& thread : threads) {
    // Spawn maxCount jobs that are all waiting on unique threads to spawn a
    // job each that increments count and then waits. The last job to
    // increment count will unblock the waiting jobs, so the group will then
    // race to tear themselves down.  On tear-down, decrement count again so
    // that it can be expected to be zero once everything's done.
    //
    // This should stress-test job submission and cancellation.
    scope.detached_spawn_on(
        thread.get_scheduler(), then(evt1.async_wait(), [&]() noexcept {
          scope.detached_spawn_on(
              thread.get_scheduler(),
              let_value_with(
                  [&] {
                    return decr{count, evt3};
                  },
                  [&](decr&) noexcept {
                    return sequence(
                        just_from([&]() noexcept {
                          auto prev =
                              count.fetch_add(1, std::memory_order_relaxed);
                          if (prev + 1 == maxCount) {
                            evt2.set();
                          }
                        }),
                        evt3.async_wait());
                  }));
        }));
  }

  // launch the race to spawn work
  evt1.set();

  // wait until count has been incremented to maxCount
  sync_wait(evt2.async_wait());

  EXPECT_EQ(count.load(std::memory_order_relaxed), maxCount);

  // launch the race to tear down
  evt3.set();

  // wait for everyone to finish tearing down
  sync_wait(scope.cleanup());

  EXPECT_EQ(count.load(std::memory_order_relaxed), 0);
}

TEST_F(async_scope_test, attach) {
  {
    auto sender = scope.attach(just());
    // attached_sender records done on async_scope
  }
  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, attach_connect) {
  mock_receiver<void()> receiver;
  auto sender = scope.attach(just());

  // the outstanding operation is "transferred" from sender to operation
  {
    auto op = connect(std::move(sender), receiver);
    // the operation is dropped w/o starting
  }
  // this will hang if record done on async_scope doesn't happen
  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, attach_copy) {
  mock_receiver<void()> receiver;
  EXPECT_CALL(*receiver, set_value()).Times(2);
  auto sender1 = scope.attach(just());
  // both senders are attached
  auto sender2 = sender1;

  // the outstanding operation is "transferred" from sender to operation
  auto op1 = connect(std::move(sender1), receiver);
  auto op2 = connect(std::move(sender2), receiver);
  static_assert(!std::is_copy_constructible_v<decltype(op1)>);
  static_assert(!std::is_move_constructible_v<decltype(op1)>);
  // auto copy = op1; is not permitted
  // auto moved = std::move(op1); is not permitted

  start(op1);
  start(op2);

  // this will hang if the transfer doesn't happen
  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, attach_copy_done) {
  mock_receiver<void()> receiver;
  EXPECT_CALL(*receiver, set_done()).Times(2);
  scope.request_stop();
  auto sender1 = scope.attach(just());
  // no more work can start on the scope
  sync_wait(when_all(scope.complete(), just_from([&]() noexcept {
                       // both senders complete as done
                       auto sender2 = sender1;
                       auto op1 = connect(std::move(sender1), receiver);
                       auto op2 = connect(std::move(sender2), receiver);

                       start(op1);
                       start(op2);
                     })));
}

TEST_F(async_scope_test, attach_copy_done2) {
  mock_receiver<void()> receiver;
  EXPECT_CALL(*receiver, set_done()).Times(2);
  auto sender1 = scope.attach(just_void_or_done(false));
  scope.request_stop();
  // no more work can start on the scope
  sync_wait(when_all(scope.complete(), just_from([&]() noexcept {
                       // both senders complete as done
                       auto sender2 = sender1;
                       auto op1 = connect(std::move(sender1), receiver);
                       auto op2 = connect(std::move(sender2), receiver);

                       start(op1);
                       start(op2);
                     })));
}

TEST_F(async_scope_test, attach_move_connect_start_just_void) {
  mock_receiver<void()> receiver;
  EXPECT_CALL(*receiver, set_value()).Times(1);
  auto sender = scope.attach(just());
  // attached_op internally uses LSB flag on async_scope*
  static_assert(alignof(async_scope) > 1);
  auto operation = connect(std::move(sender), receiver);

  start(operation);

  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, attach_move_connect_start_just_value) {
  mock_receiver<void(int)> receiver;
  EXPECT_CALL(*receiver, set_value(42)).Times(1);
  auto sender = scope.attach(just(42));
  auto operation = connect(std::move(sender), receiver);

  start(operation);

  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, attach_move_connect_start_just_done) {
  mock_receiver<void()> receiver;
  EXPECT_CALL(*receiver, set_done()).Times(1);
  auto sender = scope.attach(just_void_or_done(false));
  auto operation = connect(std::move(sender), receiver);

  start(operation);

  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, attach_request_stop_before_spawn) {
  mock_receiver<void(int)> receiver;
  EXPECT_CALL(*receiver, set_done()).Times(1);
  scope.request_stop();
  auto sender = scope.attach(just(42));
  auto operation = connect(std::move(sender), receiver);

  start(operation);
  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, attach_request_stop_before_connect) {
  mock_receiver<void(int)> receiver;
  EXPECT_CALL(*receiver, set_value(42)).Times(1);
  auto sender = scope.attach(just(42));
  scope.request_stop();
  auto operation = connect(std::move(sender), receiver);

  start(operation);

  sync_wait(scope.cleanup());
}

TEST_F(async_scope_test, attach_sync) {
  int external_context = 0;

  auto sender = scope.attach(let_value_with_stop_source([&](auto&) {
    return let_value_with_stop_token([&](auto stoken) noexcept {
      return let_value_with(
          [&external_context, stoken]() noexcept {
            return make_stop_callback(stoken, [&external_context]() noexcept {
              external_context = 42;
            });
          },
          [](auto&) noexcept -> unifex::any_sender_of<int> {
            return just_done();
          });
    });
  }));

  sync_wait(std::move(sender));
  sync_wait(scope.cleanup());
  EXPECT_EQ(external_context, 0);
}

TEST_F(async_scope_test, attach_stop_source_sync) {
  int external_context = 0;

  auto sender = scope.attach(let_value_with_stop_source([&](auto& stopSource) {
    return let_value_with_stop_token([&](auto stoken) noexcept {
      return let_value_with(
          [&external_context, stoken]() noexcept {
            return make_stop_callback(stoken, [&external_context]() noexcept {
              external_context = 42;
            });
          },
          [&](auto&) noexcept -> unifex::any_sender_of<int> {
            stopSource.request_stop();
            return just_done();
          });
    });
  }));

  sync_wait(std::move(sender));
  sync_wait(scope.cleanup());
  EXPECT_EQ(external_context, 42);
}

TEST_F(async_scope_test, attach_record_done) {
  async_manual_reset_event evt;

  struct slow_receiver {
    async_manual_reset_event& evt;
    void set_value(int) noexcept {
      auto& localEvt = evt;
      sync_wait(localEvt.async_wait());
    }

    void set_error(std::exception_ptr) noexcept {
      auto& localEvt = evt;
      sync_wait(localEvt.async_wait());
    }

    void set_done() noexcept {
      auto& localEvt = evt;
      sync_wait(when_all(localEvt.async_wait(), just_from([&]() noexcept {
                           localEvt.set();
                         })));
    }
  };

  auto operation = connect(
      scope.attach_on(thread.get_scheduler(), just(42)), slow_receiver{evt});
  start(operation);
  sync_wait(
      when_all(scope.cleanup(), just_from([&]() noexcept { evt.set(); })));
}

TEST_F(async_scope_test, attach_unstoppable_stop_token) {
  int external_context = 0;

  auto sender =
      scope.attach(let_value_with_stop_token([&](auto stoken) noexcept {
        return let_value_with(
            [&external_context, stoken]() noexcept {
              return make_stop_callback(stoken, [&external_context]() noexcept {
                external_context = 42;
              });
            },
            [](auto&) noexcept -> unifex::any_sender_of<int> {
              return just_done();
            });
      }));
  auto operation = connect(std::move(sender), UnstoppableSimpleIntReceiver{});

  start(operation);

  sync_wait(scope.cleanup());
  EXPECT_EQ(external_context, 0);
}

TEST_F(async_scope_test, attach_inplace_stoppable_stop_token) {
  int external_context = 0;
  inplace_stop_source stopSource;
  auto sender =
      scope.attach(let_value_with_stop_token([&](auto stoken) noexcept {
        return let_value_with(
            [&external_context, stoken]() noexcept {
              return make_stop_callback(stoken, [&external_context]() noexcept {
                external_context = 42;
              });
            },
            [&](auto&) noexcept -> unifex::any_sender_of<int> {
              stopSource.request_stop();
              return just_done();
            });
      }));
  auto operation =
      connect(std::move(sender), InplaceStoppableIntReceiver{stopSource});
  start(operation);

  sync_wait(scope.cleanup());
  EXPECT_EQ(external_context, 42);
}

TEST_F(async_scope_test, attach_non_inplace_stoppable_stop_token) {
  int external_context = 0;
  inplace_stop_source stopSource;
  auto sender =
      scope.attach(let_value_with_stop_token([&](auto stoken) noexcept {
        return let_value_with(
            [&external_context, stoken]() noexcept {
              return make_stop_callback(stoken, [&external_context]() noexcept {
                external_context = 42;
              });
            },
            [&](auto&) noexcept -> unifex::any_sender_of<int> {
              stopSource.request_stop();
              return just_done();
            });
      }));
  auto operation =
      connect(std::move(sender), NonInplaceStoppableIntReceiver{stopSource});
  start(operation);

  sync_wait(scope.cleanup());
  EXPECT_EQ(external_context, 42);
}

TEST_F(async_scope_test, attach_forward_cpo) {
  bool executed{false};

  auto sender = scope.attach_on(
      thread.get_scheduler(),
      allocate(then(never_sender(), [&]() noexcept { executed = true; })));

  sync_wait(sequence(
      just_from([&]() noexcept { scope.request_stop(); }),
      std::move(sender),
      scope.complete()));
  ASSERT_FALSE(executed);
}
