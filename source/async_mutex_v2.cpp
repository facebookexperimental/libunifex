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

void async_mutex::process_queue() noexcept {
  while (true) {
    waiter_base* w = queue_.pop_front();
    if (w) {
      w->resume_(w);
      return;
    }

    // Queue empty — release the lock.
    locked_.store(false, std::memory_order_release);

    // Dekker fence: orders the release before the re-check.
    std::atomic_thread_fence(std::memory_order_seq_cst);

    if (queue_.empty()) {
      return;
    }

    // Item appeared after release.  Re-acquire; if another
    // thread beat us, they will drain.
    if (locked_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
  }
}

void async_mutex::unlock() noexcept {
  process_queue();
}

}  // namespace unifex::v2
