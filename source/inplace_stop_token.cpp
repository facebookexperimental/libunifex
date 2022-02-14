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
#include <unifex/inplace_stop_token.hpp>

#include <unifex/spin_wait.hpp>

#ifndef NDEBUG
#include <stdio.h>
#endif

namespace unifex {

inplace_stop_source::~inplace_stop_source() {
  UNIFEX_ASSERT((state_.load(std::memory_order_relaxed) & locked_flag) == 0);
#ifndef NDEBUG
  for (auto* cb = callbacks_; cb != nullptr; cb = cb->next_) {
    printf("dangling inplace_stop_callback: %s\n", cb->type_name());
    fflush(stdout);
  }
#endif
  UNIFEX_ASSERT(callbacks_ == nullptr);
}

bool inplace_stop_source::request_stop() noexcept {
  if (!try_lock_unless_stop_requested(true)) {
    return true;
  }

  notifyingThreadId_ = std::this_thread::get_id();

  // We are responsible for executing callbacks.
  while (callbacks_ != nullptr) {
    auto* callback = callbacks_;
    callback->prevPtr_ = nullptr;
    callbacks_ = callback->next_;
    if (callbacks_ != nullptr) {
      callbacks_->prevPtr_ = &callbacks_;
    }

    // unlock()
    state_.store(stop_requested_flag, std::memory_order_release);

    bool removedDuringCallback = false;
    callback->removedDuringCallback_ = &removedDuringCallback;

    callback->execute();

    if (!removedDuringCallback) {
      callback->removedDuringCallback_ = nullptr;
      callback->callbackCompleted_.store(true, std::memory_order_release);
    }

    lock();
  }

  // unlock()
  state_.store(stop_requested_flag, std::memory_order_release);

  return false;
}

std::uint8_t inplace_stop_source::lock() noexcept {
  spin_wait spin;
  auto oldState = state_.load(std::memory_order_relaxed);
  do {
    while ((oldState & locked_flag) != 0) {
      spin.wait();
      oldState = state_.load(std::memory_order_relaxed);
    }
  } while (!state_.compare_exchange_weak(
      oldState,
      oldState | locked_flag,
      std::memory_order_acquire,
      std::memory_order_relaxed));

  return oldState;
}

void inplace_stop_source::unlock(std::uint8_t oldState) noexcept {
  (void)state_.store(oldState, std::memory_order_release);
}

bool inplace_stop_source::try_lock_unless_stop_requested(
    bool setStopRequested) noexcept {
  spin_wait spin;
  auto oldState = state_.load(std::memory_order_relaxed);
  do {
    while (true) {
      if ((oldState & stop_requested_flag) != 0) {
        // Stop already requested.
        return false;
      } else if (oldState == 0) {
        break;
      } else {
        spin.wait();
        oldState = state_.load(std::memory_order_relaxed);
      }
    }
  } while (!state_.compare_exchange_weak(
      oldState,
      setStopRequested ? (locked_flag | stop_requested_flag) : locked_flag,
      std::memory_order_acq_rel,
      std::memory_order_relaxed));

  // Lock acquired successfully
  return true;
}

bool inplace_stop_source::try_add_callback(
    inplace_stop_callback_base* callback) noexcept {
  if (!try_lock_unless_stop_requested(false)) {
    return false;
  }

  callback->next_ = callbacks_;
  callback->prevPtr_ = &callbacks_;
  if (callbacks_ != nullptr) {
    callbacks_->prevPtr_ = &callback->next_;
  }
  callbacks_ = callback;

  unlock(0);

  return true;
}

void inplace_stop_source::remove_callback(
    inplace_stop_callback_base* callback) noexcept {
  auto oldState = lock();

  if (callback->prevPtr_ != nullptr) {
    // Callback has not been executed yet.
    // Remove from the list.
    *callback->prevPtr_ = callback->next_;
    if (callback->next_ != nullptr) {
      callback->next_->prevPtr_ = callback->prevPtr_;
    }
    unlock(oldState);
  } else {
    auto notifyingThreadId = notifyingThreadId_;
    unlock(oldState);

    // Callback has either already been executed or is
    // currently executing on another thread.
    if (std::this_thread::get_id() == notifyingThreadId) {
      if (callback->removedDuringCallback_ != nullptr) {
        *callback->removedDuringCallback_ = true;
      }
    } else {
      // Concurrently executing on another thread.
      // Wait until the other thread finishes executing the callback.
      spin_wait spin;
      while (!callback->callbackCompleted_.load(std::memory_order_acquire)) {
        spin.wait();
      }
    }
  }
}

} // namespace unifex
