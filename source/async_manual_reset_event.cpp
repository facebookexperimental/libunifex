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

#include <unifex/async_manual_reset_event.hpp>

namespace unifex::_amre {

void async_manual_reset_event::set() noexcept {
  void* const signalledState = this;

  // replace the stack of waiting operations with a sentinel indicating we've
  // been signalled
  void* top = state_.exchange(signalledState, std::memory_order_acq_rel);

  if (top == signalledState) {
    // we were already signalled so there are no waiting operations
    return;
  }

  // We are the first thread to set the state to signalled; iteratively pop
  // the stack and complete each operation.
  auto op = static_cast<_op_base*>(top);
  while (op != nullptr) {
    std::exchange(op, op->next_)->set_value();
  }
}

void async_manual_reset_event::start_or_wait(_op_base& op, async_manual_reset_event& evt) noexcept {
  // Try to push op onto the stack of waiting ops.
  void* const signalledState = &evt;

  void* top = evt.state_.load(std::memory_order_acquire);

  do {
    if (top == signalledState) {
      // Already in the signalled state; don't push it.
      op.set_value();
      return;
    }

    // note: on the first iteration, this line transitions op.next_ from
    //       indeterminate to a well-defined value
    op.next_ = static_cast<_op_base*>(top);
  } while (!evt.state_.compare_exchange_weak(
      top,
      static_cast<void*>(&op),
      std::memory_order_release,
      std::memory_order_acquire));
}

} // namespace unfiex::_amre
