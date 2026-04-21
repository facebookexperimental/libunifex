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
#pragma once

#include <unifex/config.hpp>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <utility>

namespace unifex {
namespace _canary {

// Two synchronization mechanisms:
//
// 1. CAS on canary::watcher_ (ownership arbitration):
//    Both the canary destructor and watcher destructor try to CAS
//    watcher_ from &watcher to nullptr. Exactly one wins:
//    - Canary wins: it "owns" the watcher pointer and may access
//      watcher members. The watcher destructor spins on canary_
//      until the canary destructor signals completion.
//    - Watcher wins: it clears the pointer. The canary destructor
//      sees nullptr and does nothing.
//
// 2. Exchange on watcher::state_ (guard coordination):
//    alive(0) → guarded(1)  by watcher::alive() [CAS]
//    alive(0) → dead(2)     by canary::~canary() [exchange]
//    guarded(1) → dead(2)   by canary::~canary() [exchange, then spinloop]
//    dead(2) → done(3)      by guard::~guard() [store, unblocks spinloop]

class canary {
  static constexpr uint8_t _alive = 0;
  static constexpr uint8_t _guarded = 1;
  static constexpr uint8_t _dead = 2;
  static constexpr uint8_t _done = 3;

public:
  class watcher;

  // Returned by watcher::alive(). Truthy if the canary is still alive.
  // While the guard exists, the canary's destructor is blocked.
  class guard {
  public:
    guard(guard&& other) noexcept
      : state_(std::exchange(other.state_, nullptr)) {}
    guard& operator=(guard&&) = delete;

    explicit operator bool() const noexcept { return state_ != nullptr; }

    ~guard() noexcept {
      if (state_) {
        state_->store(_done, std::memory_order_release);
      }
    }

  private:
    friend class watcher;
    explicit guard(std::atomic<uint8_t>* state) noexcept : state_(state) {}
    std::atomic<uint8_t>* state_;
  };

  // Stack-local object that registers with the canary before a
  // potentially-destroying call. Call alive() after the call to
  // check whether the canary survived.
  class watcher {
  public:
    explicit watcher(canary& c) noexcept : canary_(&c) {
      c.watcher_.store(this, std::memory_order_release);
    }

    watcher(watcher&&) = delete;
    watcher& operator=(watcher&&) = delete;

    ~watcher() noexcept {
      if (auto* c = canary_.load(std::memory_order_acquire)) {
        watcher* expected = this;
        if (c->watcher_.compare_exchange_strong(
                expected, nullptr, std::memory_order_acq_rel)) {
          // We cleared the pointer. Canary destructor will see nullptr.
        } else {
          // Canary destructor won the CAS — it holds a reference to
          // us and will store nullptr to canary_ when done. Spin.
          while (canary_.load(std::memory_order_acquire) != nullptr) {
          }
        }
      }
    }

    // If the canary is still alive, atomically transitions to guarded
    // state and returns a truthy guard that blocks the canary's
    // destructor. If the canary has been destroyed, returns a falsy
    // guard.
    [[nodiscard]] guard alive() noexcept {
      uint8_t expected = _alive;
      if (state_.compare_exchange_strong(
              expected, _guarded, std::memory_order_acq_rel)) {
        return guard{&state_};
      }
      return guard{nullptr};
    }

  private:
    friend class canary;
    std::atomic<canary*> canary_;
    std::atomic<uint8_t> state_{_alive};
  };

  canary() noexcept = default;
  canary(canary&&) = delete;
  canary& operator=(canary&&) = delete;

  // Creates a watcher registered with this canary. The watcher is
  // non-moveable and must be used as a stack-local variable.
  // Returns a prvalue (C++17 guaranteed copy elision).
  [[nodiscard]] watcher watch() noexcept {
    UNIFEX_ASSERT(watcher_.load(std::memory_order_relaxed) == nullptr);
    return watcher{*this};
  }

  ~canary() noexcept {
    auto* w = watcher_.load(std::memory_order_acquire);
    if (!w) {
      return;
    }
    // Try to claim ownership of the watcher pointer.
    if (!watcher_.compare_exchange_strong(
            w, nullptr, std::memory_order_acq_rel)) {
      // Watcher destructor won — it cleared the pointer. Done.
      return;
    }
    // We own w. The watcher destructor will spin on canary_ until
    // we signal completion.
    auto old = w->state_.exchange(_dead, std::memory_order_acq_rel);
    if (old == _guarded) {
      // Guard is held — spin until it is released.
      while (w->state_.load(std::memory_order_acquire) != _done) {
      }
    }
    // Signal the watcher destructor that we're done with its members.
    w->canary_.store(nullptr, std::memory_order_release);
  }

private:
  friend class watcher;
  std::atomic<watcher*> watcher_{nullptr};
};

}  // namespace _canary

using _canary::canary;

}  // namespace unifex
