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
#include <unifex/timed_single_thread_context.hpp>

namespace unifex {

timed_single_thread_context::timed_single_thread_context()
: thread_([this] { this->run(); })
{}

timed_single_thread_context::~timed_single_thread_context() {
  {
    std::lock_guard lock{mutex_};
    stop_ = true;
    cv_.notify_one();
  }
  thread_.join();

  UNIFEX_ASSERT(head_ == nullptr);
}

void timed_single_thread_context::enqueue(task_base* task) noexcept {
  std::lock_guard lock{mutex_};

  if (head_ == nullptr || task->dueTime_ < head_->dueTime_) {
    // Insert at the head of the queue.
    task->next_ = head_;
    task->prevNextPtr_ = &head_;
    if (head_ != nullptr) {
      head_->prevNextPtr_ = &task->next_;
    }
    head_ = task;

    // New minimum due-time has changed, wake the thread.
    cv_.notify_one();
  } else {
    auto* queuedTask = head_;
    while (queuedTask->next_ != nullptr &&
           queuedTask->next_->dueTime_ <= task->dueTime_) {
      queuedTask = queuedTask->next_;
    }

    // Insert after queuedTask
    task->prevNextPtr_ = &queuedTask->next_;
    task->next_ = queuedTask->next_;
    if (task->next_ != nullptr) {
      task->next_->prevNextPtr_ = &task->next_;
    }
    queuedTask->next_ = task;
  }
}

void timed_single_thread_context::run() {
  std::unique_lock lock{mutex_};

  while (!stop_) {
    if (head_ != nullptr) {
      auto now = clock_t::now();
      auto nextDueTime = head_->dueTime_;
      if (nextDueTime <= now) {
        // Ready to run

        // Dequeue item
        auto* task = head_;
        head_ = task->next_;
        if (head_ != nullptr) {
          head_->prevNextPtr_ = &head_;
        }

        // Flag the task as dequeued.
        task->prevNextPtr_ = nullptr;
        lock.unlock();

        task->execute();

        lock.lock();
      } else {
        // Not yet ready to run. Sleep until it's ready.
        cv_.wait_until(lock, nextDueTime);
      }
    } else {
      // Queue is empty.
      cv_.wait(lock);
    }
  }
}

void _timed_single_thread_context::cancel_callback::operator()() noexcept {
  std::unique_lock lock{task_->context_->mutex_};
  auto now = clock_t::now();
  if (now < task_->dueTime_) {
    task_->dueTime_ = now;

    if (task_->prevNextPtr_ != nullptr) {
      // Task is still in the queue, dequeue and requeue it.

      // Remove from the queue.
      *task_->prevNextPtr_ = task_->next_;
      if (task_->next_ != nullptr) {
        task_->next_->prevNextPtr_ = task_->prevNextPtr_;
      }
      task_->prevNextPtr_ = nullptr;
      lock.unlock();

      // And requeue with an updated time.
      task_->dueTime_ = now;
      task_->context_->enqueue(task_);
    }
  }
}

} // namespace unifex
