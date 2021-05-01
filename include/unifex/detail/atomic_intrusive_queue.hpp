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
#pragma once

#include <unifex/detail/intrusive_queue.hpp>
#include <unifex/detail/intrusive_stack.hpp>

#include <atomic>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {

// An intrusive queue that supports multiple threads concurrently
// enqueueing items to the queue and a single consumer dequeuing
// items from the queue.
//
// The consumer always dequeues all items at once, resetting the
// queue back to empty.
//
// The consumer can also mark itself as inactive. The next producer
// to enqueue an item will then be notified that the consuemr is
// inactive by the return-value of enqueue(). This producer is
// then responsible for waking up the consumer.
template <typename Item, Item* Item::*Next>
class atomic_intrusive_queue {
public:
  atomic_intrusive_queue() noexcept : head_(nullptr) {}

  explicit atomic_intrusive_queue(bool initiallyActive) noexcept
      : head_(initiallyActive ? nullptr : producer_inactive_value()) {}

  ~atomic_intrusive_queue() {
    // Check that all items in this queue have beel dequeued.
    // Not doing so is likely a bug in the code.
    UNIFEX_ASSERT(
        head_.load(std::memory_order_relaxed) == nullptr ||
        head_.load(std::memory_order_relaxed) == producer_inactive_value());
  }

  // Disable move/copy construction/assignment
  atomic_intrusive_queue(const atomic_intrusive_queue&) = delete;
  atomic_intrusive_queue(atomic_intrusive_queue&&) = delete;
  atomic_intrusive_queue& operator=(const atomic_intrusive_queue&) = delete;
  atomic_intrusive_queue& operator=(atomic_intrusive_queue&&) = delete;

  // Returns true if the previous state was inactive and this
  // operation successfully marked it as active.
  // Returns false if the previous state was active.
  [[nodiscard]] bool try_mark_active() noexcept {
    void *oldValue = producer_inactive_value();
    return head_.compare_exchange_strong(oldValue, nullptr,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed);
  }

  // Either enqueue an item to the queue if the producer is
  // active, otherwise if the producer is inactive then
  // mark the queue as active but do not enqueue the item.
  // The assumption is that the caller then becomes the
  // producer and will process item directly.
  //
  // Returns true if the item was enqueued.
  // Returns false if the item was not enqueued and the queue
  // was transitioned from inactive to active.
  [[nodiscard]] bool enqueue_or_mark_active(Item *item) noexcept {
    void *const inactive = producer_inactive_value();
    void *oldValue = head_.load(std::memory_order_relaxed);
    void *newValue;
    do {
      if (oldValue == inactive) {
        newValue = nullptr;
      } else {
        item->*Next = static_cast<Item *>(oldValue);
        newValue = item;
      }
    } while (!head_.compare_exchange_weak(oldValue, newValue,
                                          std::memory_order_acq_rel));
    return oldValue != inactive;
  }

  // Enqueue an item to the queue.
  //
  // Returns true if the producer is inactive and needs to be
  // woken up. The calling thread has responsibility for waking
  // up the producer.
  [[nodiscard]] bool enqueue(Item* item) noexcept {
    void* const inactive = producer_inactive_value();
    void* oldValue = head_.load(std::memory_order_relaxed);
    do {
      item->*Next =
          (oldValue == inactive) ? nullptr : static_cast<Item*>(oldValue);
    } while (!head_.compare_exchange_weak(
        oldValue, item, std::memory_order_acq_rel));
    return oldValue == inactive;
  }

  // Dequeue all items. Resetting the queue back to empty.
  // Not valid to call if the producer is inactive.
  [[nodiscard]] intrusive_queue<Item, Next> dequeue_all() noexcept {
    void* value = head_.load(std::memory_order_relaxed);
    if (value == nullptr) {
      // Queue is empty, return empty queue.
      return {};
    }
    UNIFEX_ASSERT(value != producer_inactive_value());

    value = head_.exchange(nullptr, std::memory_order_acquire);
    UNIFEX_ASSERT(value != producer_inactive_value());
    UNIFEX_ASSERT(value != nullptr);

    return intrusive_queue<Item, Next>::make_reversed(
        static_cast<Item*>(value));
  }

  [[nodiscard]] intrusive_stack<Item, Next> dequeue_all_reversed() noexcept {
    void* value = head_.load(std::memory_order_relaxed);
    if (value == nullptr) {
      // Queue is empty, return empty queue.
      return {};
    }
    UNIFEX_ASSERT(value != producer_inactive_value());

    value = head_.exchange(nullptr, std::memory_order_acquire);
    UNIFEX_ASSERT(value != producer_inactive_value());
    UNIFEX_ASSERT(value != nullptr);

    return intrusive_stack<Item, Next>::adopt(static_cast<Item*>(value));
  }

  [[nodiscard]] bool try_mark_inactive() noexcept {
    void* const inactive = producer_inactive_value();
    void* oldValue = head_.load(std::memory_order_relaxed);
    if (oldValue == nullptr) {
      if (head_.compare_exchange_strong(
              oldValue,
              inactive,
              std::memory_order_release,
              std::memory_order_relaxed)) {
        // Successfully marked as inactive
        return true;
      }
    }

    // The queue was 
    UNIFEX_ASSERT(oldValue != nullptr);
    UNIFEX_ASSERT(oldValue != inactive);
    return false;
  }

  // Atomically either mark the producer as inactive if the queue was empty
  // or dequeue pending items from the queue.
  //
  // Not valid to call if the producer is already marked as inactive.
  [[nodiscard]] intrusive_queue<Item, Next>
  try_mark_inactive_or_dequeue_all() noexcept {
    if (try_mark_inactive()) {
      return {};
    }

    void* oldValue = head_.exchange(nullptr, std::memory_order_acquire);
    UNIFEX_ASSERT(oldValue != nullptr);
    UNIFEX_ASSERT(oldValue != producer_inactive_value());

    return intrusive_queue<Item, Next>::make_reversed(
        static_cast<Item*>(oldValue));
  }

 private:
  void* producer_inactive_value() const noexcept {
    // Pick some pointer that is not nullptr and that is
    // guaranteed to not be the address of a valid item.
    return const_cast<void*>(static_cast<const void*>(&head_));
  }

  std::atomic<void*> head_;
};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
