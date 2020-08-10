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
#include <unifex/fifo_manual_event_loop.hpp>

namespace unifex {
namespace _fifo_manual_event_loop {

void context::run() {
  std::unique_lock lock{mutex_};
  while (true) {
    while (head_ == nullptr) {
      if (stop_) return;
      cv_.wait(lock);
    }
    std::cout << "Run loop iteration\n";
    auto* task = head_;
    head_ = task->next_;
    if (head_ == nullptr) {
      tail_ = nullptr;
    }
    lock.unlock();
    std::cout << "\tExecute in loop " << task << "\n";
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
  std::cout << "Enqueue " << task << "\n";
  if (head_ == nullptr) {
  std::cout << "\tEnqueue as head" << task << "\n";
    head_ = task;
  } else {
    std::cout << "\tEnqueue as tail" << task << "\n";
    tail_->next_ = task;
  }
  tail_ = task;
  task->next_ = nullptr;
  cv_.notify_one();
}

} // _fifo_manual_event_loop
} // unifex
