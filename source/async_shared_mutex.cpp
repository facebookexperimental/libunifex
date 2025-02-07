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

#include <unifex/async_shared_mutex.hpp>

#include <unifex/async_mutex.hpp>
#include <unifex/sync_wait.hpp>

#include <list>

namespace unifex {

async_shared_mutex::async_shared_mutex() noexcept
  : activeUniqueCount_(0)
  , activeSharedCount_(0) {
}

async_shared_mutex::~async_shared_mutex() {
}

bool async_shared_mutex::try_enqueue(waiter_base* base, bool unique) noexcept {
  unifex::sync_wait(mutex_.async_lock());

  if (activeUniqueCount_ == 0 && activeSharedCount_ == 0) {
    UNIFEX_ASSERT(pendingQueue_.empty());
    if (unique) {
      activeUniqueCount_++;
    } else {
      activeSharedCount_++;
    }
    mutex_.unlock();
    return false;
  }

  if (!unique && activeUniqueCount_ == 0 && pendingQueue_.empty()) {
    UNIFEX_ASSERT(activeSharedCount_ > 0);
    activeSharedCount_++;
    mutex_.unlock();
    return false;
  }

  pendingQueue_.push_back(std::make_pair(base, unique));
  mutex_.unlock();
  return true;
}

void async_shared_mutex::unlock() noexcept {
  unifex::sync_wait(mutex_.async_lock());
  UNIFEX_ASSERT(activeUniqueCount_ == 1);
  UNIFEX_ASSERT(activeSharedCount_ == 0);
  activeUniqueCount_--;

  std::list<waiter_base*> waiters;
  while (!pendingQueue_.empty() && activeUniqueCount_ == 0 &&
         (!pendingQueue_.front().second ||
          (pendingQueue_.front().second && activeSharedCount_ == 0))) {
    auto item = pendingQueue_.front();
    pendingQueue_.pop_front();
    if (item.second) {
      activeUniqueCount_++;
    } else {
      activeSharedCount_++;
    }
    waiters.push_back(item.first);
  }
  UNIFEX_ASSERT(activeUniqueCount_ <= 1);
  UNIFEX_ASSERT(!(activeUniqueCount_ > 0 && activeSharedCount_ > 0));
  mutex_.unlock();

  while (!waiters.empty()) {
    waiter_base* waiter = waiters.front();
    waiters.pop_front();
    waiter->resume_(waiter);
  }
}

void async_shared_mutex::unlock_shared() noexcept {
  unifex::sync_wait(mutex_.async_lock());
  UNIFEX_ASSERT(activeUniqueCount_ == 0);
  UNIFEX_ASSERT(activeSharedCount_ > 0);
  activeSharedCount_--;

  waiter_base* waiter = nullptr;
  if (activeSharedCount_ == 0 && !pendingQueue_.empty()) {
    auto item = pendingQueue_.front();
    pendingQueue_.pop_front();
    UNIFEX_ASSERT(item.second);
    activeUniqueCount_++;
    waiter = item.first;
  }
  mutex_.unlock();

  if (waiter) {
    waiter->resume_(waiter);
  }
}

}  // namespace unifex
