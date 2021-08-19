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

#include <unifex/async_mutex.hpp>


namespace unifex {

async_mutex::async_mutex() noexcept : atomicQueue_(false) {}

async_mutex::~async_mutex() {}

bool async_mutex::try_enqueue(waiter_base *base) noexcept {
  return atomicQueue_.enqueue_or_mark_active(base);
}

void async_mutex::unlock() noexcept {
  if (pendingQueue_.empty()) {
    auto newWaiters = atomicQueue_.try_mark_inactive_or_dequeue_all();
    if (newWaiters.empty()) {
      return;
    }
    pendingQueue_ = std::move(newWaiters);
  }

  waiter_base *item = pendingQueue_.pop_front();
  item->resume_(item);
}

} // namespace unifex
