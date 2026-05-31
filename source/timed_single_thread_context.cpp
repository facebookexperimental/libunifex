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
  : thread_([this] { this->run(); }) {
}

timed_single_thread_context::~timed_single_thread_context() {
  {
    std::lock_guard lock{mutex_};
    stop_ = true;
    cv_.notify_one();
  }
  thread_.join();

  UNIFEX_ASSERT(heap_.empty());
}

void timed_single_thread_context::enqueue(task_base* task) noexcept {
  std::lock_guard lock{mutex_};

  bool wasEmpty = heap_.empty();
  heap_.insert(task);
  if (wasEmpty) {
      cv_.notify_one();
  } else {
    auto now = clock_t::now();
    if (task->dueTime_ <= now) {
        // New task is ready, wake the thread.
        cv_.notify_one();
    }
  }

}

void timed_single_thread_context::run() {
  std::unique_lock lock{mutex_};

  while (!stop_) {
    if (!heap_.empty()) {
      auto now = clock_t::now();
      auto nextDueTime = heap_.top()->dueTime_;
      if (nextDueTime <= now) {
        // Ready to run

        // Dequeue item
        auto* task = heap_.pop();
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

    if (task_->context_->heap_.remove(task_)) {
      // Task was still in the queue, requeue it.
      lock.unlock();

      // And requeue with an updated time.
      task_->dueTime_ = now;
      task_->context_->enqueue(task_);
    }
  }
}

}  // namespace unifex
