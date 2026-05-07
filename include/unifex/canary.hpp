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

// Destructor arbitration uses tagged pointers (LSB = locked) on
// both canary::watcher_ and watcher::canary_. Each destructor
// locks its own pointer first, then accesses the other object.
// Locking prevents the other destructor from completing (its CAS
// on our pointer fails), ensuring the other object stays alive
// while we access it.
//
// Deadlock (both pointers locked) is resolved in favor of watcher:
// canary unlocks watcher_ and spins until the watcher clears it.
//
// Guard coordination uses watcher::state_ (unchanged):
//   alive(0) → guarded(1)  by watcher::alive() [CAS]
//   alive(0) → dead(2)     by canary::~canary() [exchange]
//   guarded(1) → dead(2)   by canary::~canary() [exchange, then spinloop]
//   dead(2) → done(3)      by guard::~guard() [store, unblocks spinloop]

class canary {
  static constexpr uint8_t _alive = 0;
  static constexpr uint8_t _guarded = 1;
  static constexpr uint8_t _dead = 2;
  static constexpr uint8_t _done = 3;

  static constexpr uintptr_t _lock_bit = 1;

  template <typename T>
  static T* _lock(T* p) noexcept {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(p) | _lock_bit);
  }

  template <typename T>
  static T* _unlock(T* p) noexcept {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(p) & ~_lock_bit);
  }

  template <typename T>
  static bool _is_locked(T* p) noexcept {
    return reinterpret_cast<uintptr_t>(p) & _lock_bit;
  }

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

  class watcher {
  public:
    explicit watcher(canary& c) noexcept : canary_(&c) {
      c.watcher_.store(this, std::memory_order_release);
    }

    watcher(watcher&&) = delete;
    watcher& operator=(watcher&&) = delete;

    ~watcher() noexcept {
      // Step 1: lock our own pointer (canary_).
      auto* c = canary_.load(std::memory_order_relaxed);
      if (!c) {
        return;
      }
      if (_is_locked(c) ||
          !canary_.compare_exchange_strong(
              c, _lock(c), std::memory_order_acq_rel)) {
        // Canary destructor locked or cleared canary_. It will
        // store nullptr when done. Spin.
        while (canary_.load(std::memory_order_acquire) != nullptr) {
        }
        return;
      }
      // canary_ is now locked. Canary destructor can't complete.

      // Step 2: clear canary's watcher_ pointer.
      // Canary is alive (its destructor can't complete while our
      // canary_ is locked — its CAS on canary_ will fail).
      watcher* expected = this;
      while (!c->watcher_.compare_exchange_weak(
          expected, nullptr, std::memory_order_acq_rel)) {
        if (expected == nullptr) {
          break;  // shouldn't happen, but handle gracefully
        }
        // watcher_ is locked (this|1) by canary destructor.
        // Canary will detect deadlock and unlock watcher_.
        // Spin-retry.
        expected = this;
      }

      // Step 3: unlock and clear canary_.
      canary_.store(nullptr, std::memory_order_release);
    }

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

  static_assert(
      alignof(watcher) >= 2,
      "watcher must be at least 2-byte aligned for LSB tagging");

  canary() noexcept = default;
  canary(canary&&) = delete;
  canary& operator=(canary&&) = delete;

  [[nodiscard]] watcher watch() noexcept {
    UNIFEX_ASSERT(watcher_.load(std::memory_order_relaxed) == nullptr);
    return watcher{*this};
  }

  ~canary() noexcept {
    // Step 1: lock our own pointer (watcher_).
    auto* w = watcher_.load(std::memory_order_relaxed);
    if (!w) {
      return;
    }
    if (_is_locked(w) ||
        !watcher_.compare_exchange_strong(
            w, _lock(w), std::memory_order_acq_rel)) {
      // Watcher destructor locked or cleared watcher_. Done.
      return;
    }
    // watcher_ is now locked. Watcher destructor can't complete.

    // Step 2: lock watcher's canary_ pointer.
    // Watcher is alive (its destructor can't complete while our
    // watcher_ is locked — its CAS on watcher_ will fail).
    canary* expected = this;
    if (!w->canary_.compare_exchange_strong(
            expected, _lock(this), std::memory_order_acq_rel)) {
      // canary_ is locked (this|1) by watcher destructor. Deadlock.
      // Canary yields: unlock watcher_ and let the watcher proceed.
      watcher_.store(w, std::memory_order_release);  // unlock
      // Watcher will clear watcher_ to nullptr. Spin until done.
      while (watcher_.load(std::memory_order_acquire) != nullptr) {
      }
      return;
    }
    // Both pointers locked. We fully own the watcher.

    // Step 3: guard coordination via state_.
    auto old = w->state_.exchange(_dead, std::memory_order_acq_rel);
    if (old == _guarded) {
      // Guard is held — spin until released. The guard destructor
      // overwrites _dead with _done; we spin while state_ remains
      // _dead (the value we wrote) rather than waiting for a
      // specific successor value.
      while (w->state_.load(std::memory_order_acquire) == _dead) {
      }
    }

    // Step 4: signal completion and unlock.
    w->canary_.store(nullptr, std::memory_order_release);
    watcher_.store(nullptr, std::memory_order_release);
  }

private:
  friend class watcher;
  std::atomic<watcher*> watcher_{nullptr};
};

static_assert(
    alignof(canary) >= 2,
    "canary must be at least 2-byte aligned for LSB tagging");

}  // namespace _canary

using _canary::canary;

}  // namespace unifex
