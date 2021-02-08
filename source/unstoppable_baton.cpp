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

#include <unifex/unstoppable_baton.hpp>

namespace unifex::_ub {

void unstoppable_baton::post() noexcept {
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

void unstoppable_baton::start_or_wait(_op_base& op, unstoppable_baton& baton) noexcept {
  // Try to push op onto the stack of waiting ops.
  void* const signalledState = &baton;

  void* top = baton.state_.load(std::memory_order_acquire);

  do {
    if (top == signalledState) {
      // Already in the signalled state; don't push it.
      op.set_value();
      return;
    }

    // note: on the first iteration, this line transitions op.next_ from
    //       indeterminate to a well-defined value
    op.next_ = static_cast<_op_base*>(top);
  } while (!baton.state_.compare_exchange_weak(
      top,
      static_cast<void*>(&op),
      std::memory_order_release,
      std::memory_order_acquire));
}

} // namespace unfiex::_ub
