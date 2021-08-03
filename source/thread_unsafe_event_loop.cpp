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
#include <unifex/thread_unsafe_event_loop.hpp>

#include <thread>

namespace unifex {

void _thread_unsafe_event_loop::cancel_callback::operator()() noexcept {
  auto now = clock_t::now();
  if (now < op_->dueTime_) {
    op_->dueTime_ = now;

    if (op_->prevPtr_ != nullptr) {
      // Task is still in the queue, dequeue and requeue it.

      // Remove from the queue.
      *op_->prevPtr_ = op_->next_;
      if (op_->next_ != nullptr) {
        op_->next_->prevPtr_ = op_->prevPtr_;
      }

      // And requeue with an updated time.
      op_->dueTime_ = now;
      op_->loop_.enqueue(op_);
    }
  }
}

void thread_unsafe_event_loop::enqueue(operation_base* op) noexcept {
  auto* current = head_;
  if (current == nullptr || op->dueTime_ < current->dueTime_) {
    // insert at head of list
    head_ = op;
    op->prevPtr_ = &head_;
    op->next_ = current;
    if (current != nullptr) {
      current->prevPtr_ = &op->next_;
    }
  } else {
    // Traverse the list until we find the item we should
    // be inserted after.
    while (current->next_ != nullptr &&
           current->next_->dueTime_ <= op->dueTime_) {
      current = current->next_;
    }

    // Insert after 'current'
    op->next_ = current->next_;
    if (op->next_ != nullptr) {
      op->next_->prevPtr_ = &op->next_;
    }
    op->prevPtr_ = &current->next_;
    current->next_ = op;
  }
}

void thread_unsafe_event_loop::run_until_empty() noexcept {
  auto lastTime = clock_t::now();
  while (head_ != nullptr) {
    if (head_->dueTime_ > lastTime) {
      lastTime = clock_t::now();
      if (head_->dueTime_ > lastTime) {
        std::this_thread::sleep_until(head_->dueTime_);
        lastTime = clock_t::now();
      }
    }

    auto* item = head_;
    head_ = item->next_;
    if (head_ != nullptr) {
      head_->prevPtr_ = &head_;
    }
    item->execute();
  }
}

} // namespace unifex
