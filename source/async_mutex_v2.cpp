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

#include <unifex/v2/async_mutex.hpp>

namespace unifex::v2 {

bool async_mutex::try_enqueue(waiter_base* waiter) noexcept {
  if (try_lock()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push_back(waiter);
  return true;
}

bool async_mutex::try_dequeue(waiter_base* waiter) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (waiter->enqueued()) {
    queue_.remove(waiter);
    waiter->set_dequeued();
    return true;
  }
  return false;
}

void async_mutex::unlock() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) {
    locked_.store(false);
  } else {
    waiter_base* next{queue_.pop_front()};
    next->set_dequeued();
    next->resume_(next);
  }
}

}  // namespace unifex::v2
