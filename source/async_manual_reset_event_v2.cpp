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

#include <unifex/v2/async_manual_reset_event.hpp>

namespace unifex::v2 {

void async_manual_reset_event::set() noexcept {
  // Atomically latch (mark signalled) and move all waiters
  // into a stack-local list.  After this call the event is
  // latched and empty; the event object is not touched again,
  // so it is safe even if a waiter's completion destroys it.
  atomic_intrusive_list<waiter_base, true> local;
  waiters_.latch_and_drain(local);

  // pop_front sets self=nullptr just before returning each
  // item, so try_remove from a concurrent stop callback can
  // still succeed on items not yet popped.
  while (auto* w = local.pop_front()) {
    w->resume_(w);
  }
}

}  // namespace unifex::v2
