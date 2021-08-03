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
#include <unifex/manual_event_loop.hpp>

namespace unifex {
namespace _manual_event_loop {

void context::run() {
  std::unique_lock lock{mutex_};
  while (true) {
    while (head_ == nullptr) {
      if (stop_) return;
      cv_.wait(lock);
    }
    auto* task = head_;
    head_ = task->next_;
    if (head_ == nullptr) {
      tail_ = nullptr;
    }
    lock.unlock();
    task->execute();
    lock.lock();
  }
}

void context::stop() {
  std::unique_lock lock{mutex_};
  stop_ = true;
  cv_.notify_all();
}

void context::enqueue(task_base* task) {
  std::unique_lock lock{mutex_};
  if (head_ == nullptr) {
    head_ = task;
  } else {
    tail_->next_ = task;
  }
  tail_ = task;
  task->next_ = nullptr;
  cv_.notify_one();
}

} // _manual_event_loop
} // unifex
