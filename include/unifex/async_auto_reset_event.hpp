// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <unifex/async_manual_reset_event.hpp>
#include <unifex/defer.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_void_or_done.hpp>
#include <unifex/let_value.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/let_value_with_stop_token.hpp>
#include <unifex/sender_concepts.hpp>

#include <mutex>

namespace unifex {

namespace _aare {
/**
 * A *Stream* backed by an auto-reset event.
 */
struct async_auto_reset_event final {
  /**
   * Initializes an async_auto_reset_event in the unset state.
   */
  async_auto_reset_event() noexcept : async_auto_reset_event(false) {}

  /**
   * Initializes an async_auto_reset_event.
   *
   * If startReady is true, the event is initialized in the set state; otherwise
   * it's initialized in the unset state.
   */
  explicit async_auto_reset_event(bool startReady) noexcept
    : state_(startReady ? state::SET : state::UNSET)
    , event_(startReady) {}

  /**
   * Puts the event in the set state if it's not in the done state.
   *
   * Signals any waiting *Senders* if the event was unset.
   */
  void set() noexcept;

  /**
   * Puts the event in the done state.
   *
   * Signals any waiting *Senders* if the event was unset.
   */
  void set_done() noexcept;

  struct stream_view;
  /**
   * Retrieves a *Stream*-shaped view of the event.
   */
  stream_view stream() noexcept;

private:
  enum class state { UNSET, SET, DONE };
  /**
   * Idea for a lockless version if `state_` can be collapsed:
   *
   * `state_` variable could become an atomic and the state transitions become
   * relaxed swaps or assignments followed by evet_.set() when appropriate. The
   * problem is that we have a three-state `state_` that's trying to stay
   * consistent with the two-state `event_`.
   * 1. `event_ is ready()`: `state_ is either `SET` or `DONE`
   * 2. `event_` is !ready(): `state_` is `UNSET`
   *
   * It's reasonable to require `set()` and `set_done()` to be called from the
   * same thread, but `try_reset()` is going to be called by the consumer,
   * likely from another thread. If the event only supported `set()` and
   * `try_reset()`, we could drop the mutex but, since we want `try_reset()` to
   * fail forever once `set_done()` a mutex is used.
   */
  std::mutex mutex_;
  state state_{state::UNSET};
  unifex::async_manual_reset_event event_;

  /**
   * Tries to move the event from set to unset.
   *
   * Returns true if the event has been unset, or false if it's been
   * cancelled.
   */
  bool try_reset() noexcept;
};

struct async_auto_reset_event::stream_view final {
  explicit stream_view(async_auto_reset_event* evt) noexcept : evt_(evt) {
    UNIFEX_ASSERT(evt_ != nullptr);
  }

  /**
   * Returns a *Sender* that completes with set_value when the event is set.
   */
  auto next() noexcept {
    return unifex::let_value_with_stop_token([evt = evt_](
                                                 auto stopToken) noexcept {
      return unifex::let_value_with(
          [stopToken, evt]() noexcept {
            // unifex::async_manual_reset_event::async_wait() returns an
            // unstoppable *Sender* so, to support prompt cancellation
            // of the *Sender* we're composing here, we register a stop
            // callback with our *Receiver* that responds to stop
            // requests by transitioning the auto-reset event to the
            // done state. When the auto-reset event transitions to the
            // done state, anyone waiting on our next-sender will wake
            // up and complete with set_done.
            //
            // This strategy matches the *Stream* contract because
            // cancelling the result of next(stream) is expected to
            // cancel the whole stream.  The streams returned from
            // event.stream() are light weight handles to the overall
            // event and are indistinguishable from each other so
            // cancelling one such stream by cancelling its next-sender
            // is interpreted as cancelling the whole event.
            auto stopCallback = [evt]() noexcept {
              evt->set_done();
            };

            // avoid inline `Scheduler`: narrow margin for a lifetime issue
            // during synchronous cancellation
            //
            // 1. when cancelling on the same `Scheduler` that's waiting on
            // `async_wait()`: the stop callback never synchronously wakes the
            // event because `evt->set_done()` always schedules the wakeup
            //
            // 2. when cancelling from a different `Scheduler` than the one
            // that's waiting on `async_wait()`: there's a race between
            // completing the stop callback and completing the `async_wait()`
            // but the `let_value_with` operation state will not complete the
            // downstream `Receiver` until the stop callback has been destroyed,
            // which will synchronize with the completion of the callback; given
            // this scenario is "cancel and complete on different Schedulers",
            // the synchronization will block rather than no-op
            using stop_token_t = unifex::remove_cvref_t<decltype(stopToken)>;
            using stop_callback_t =
                typename stop_token_t::template callback_type<
                    decltype(stopCallback)>;

            return stop_callback_t{stopToken, stopCallback};
          },
          [evt](auto&) noexcept {
            return unifex::let_value(
                evt->event_.async_wait(), [evt]() noexcept {
                  return unifex::just_void_or_done(evt->try_reset());
                });
          });
    });
  }

  /**
   * Returns a *Sender* that puts the event in the done state and then
   * completes with set_done.
   */
  auto cleanup() noexcept {
    return unifex::defer([evt = evt_]() noexcept {
      evt->set_done();
      return unifex::just_done();
    });
  }

private:
  async_auto_reset_event* evt_;
};

inline async_auto_reset_event::stream_view
async_auto_reset_event::stream() noexcept {
  return async_auto_reset_event::stream_view{this};
}

}  // namespace _aare
using _aare::async_auto_reset_event;
}  // namespace unifex
