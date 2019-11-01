/*
 * Copyright 2019-present Facebook, Inc.
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
#include <unifex/manual_event_loop.hpp>

namespace unifex {

void manual_event_loop::run() {
  std::unique_lock lock{mutex_};
  while (!stop_) {
    while (head_ == nullptr) {
      cv_.wait(lock);
      if (stop_)
        return;
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

void manual_event_loop::stop() {
  std::unique_lock lock{mutex_};
  stop_ = true;
  cv_.notify_all();
}

void manual_event_loop::enqueue(task_base* task) {
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

} // unifex
