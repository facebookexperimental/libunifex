/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <unifex/unnamed_primitive.hpp>

namespace unifex::_unnamed {

namespace {

void complete_operation(std::uintptr_t opAddr) noexcept {
  auto op = reinterpret_cast<_op_base*>(opAddr);
  op->complete();
}

void clear_cancelled_state(
    std::atomic<std::uintptr_t>& state, std::uintptr_t cancelled) noexcept {

  // if state is still in the cancelled state, return it to the empty state
  (void)state.compare_exchange_strong(
      cancelled,
      to_addr(nullptr),
      // we're not synchronizing with anything here
      std::memory_order_relaxed);
}

} // namespace

void unnamed_primitive::set() noexcept {
  std::uintptr_t signalledState = to_addr(this);
  const std::uintptr_t cancelledState = signalledState + 1;

  // replace the waiting operation with a sentinel indicating we've been
  // signalled
  std::uintptr_t opAddr = state_.exchange(signalledState, std::memory_order_acq_rel);

  if (opAddr == signalledState ||
      opAddr == cancelledState ||
      opAddr == to_addr(nullptr)) {
    // we were:
    //  - already signalled,
    //  - cancelled, or
    //  - there was no waiting operation.
    return;
  }

  // resume the waiting operation
  complete_operation(opAddr);
}

void unnamed_primitive::cancel(_op_base* op) noexcept {
  // This is only invoked from the stop_callback, so our stop_token has
  // requested cancellation.
  //
  // We're about to read-modify-write state_, which we will observe to be in one
  // of three states:
  //
  //  1. it was nullptr; we must be getting cancelled while we're still in
  //     start() and start_or_wait hasn't updated state_, yet.
  //
  //  2. it was signalled; we could be in one of two sub-states:
  //    a. we're still in start() and state_ was signalled before the call to
  //       start_or_wait, or
  //    b. start() completed by registering us to wait and cancellation lost a
  //       race to signalling.
  //
  //  3. it was opAddr; we were waiting for a signal but cancellation happened
  //     before the signal arrived.
  //
  // If we observe anything else, there's a bug somewhere.
  //
  // We're going to assume we're in state 3, and respond appropriately if not.
  //
  // In state 1, we need to CAS again from nullptr to cancelled so that the call
  // to start_or_wait can observe we've been cancelled.  If the CAS from nullptr
  // succeeds then we've been successful and we'll need to invoke set_done after
  // destroying the callback, which we'll do in start_or_wait.  We can't enter
  // state 3 from here because we're being cancelled before calling
  // start_or_wait, which is the only way to update state_ to opAddr, so if the
  // CAS from nullptr fails then we must have transitioned to state 2a.
  //
  // In state 2a, we'll do nothing, start_or_wait will see that state_ is
  // signalled, and immediately invoke complete, whereupon we'll check whether
  // the stop_token has had stop requested (which it has), and we'll invoke
  // set_done instead of set_value.
  //
  // In state 2b, the signalling thread is racing with the cancelling thread,
  // which will be synchronized in complete when we destroy the stop callback.
  // We'll then check whether the stop token has had stop requested, find that
  // it has, and invoke set_done so we'll do nothing here.
  //
  // In state 3, our first CAS will succeed, signalling in state_ that we've
  // been cancelled.  No future signal attempt will invoke complete so we'll
  // call unifex::set_done ourselves after destroying the callback.

  const auto opAddr = to_addr(op);
  const auto signalledState = to_addr(this);
  const auto cancelledState = signalledState + 1;

  auto expectedState = opAddr;

  if (state_.compare_exchange_strong(
      expectedState,
      cancelledState,
      std::memory_order_acq_rel)) {
    // state 3

    // leave it in the no-waiter state
    clear_cancelled_state(state_, cancelledState);

    complete_operation(opAddr);
    return;
  }

  if (expectedState == to_addr(nullptr)) {
    // state 1
    if (state_.compare_exchange_strong(
        expectedState,
        cancelledState,
        std::memory_order_acq_rel)) {
      // do nothing and let start_or_wait invoke complete
      return;
    }
  }

  if (expectedState == signalledState) {
    // state 2 (a or b) or failed retry in state 1
    return;
  }
  else {
    std::terminate();
  }
}


void unnamed_primitive::start_or_wait(_op_base* op) noexcept {
  const std::uintptr_t opAddr = to_addr(op);
  const std::uintptr_t signalledState = to_addr(this);
  const std::uintptr_t cancelledState = signalledState + 1;

  std::uintptr_t expectedState = to_addr(nullptr);

  if (state_.compare_exchange_strong(
      expectedState,
      opAddr,
      std::memory_order_acq_rel)) {
    // no one was waiting and it wasn't signalled, so now we're waiting
    return;
  }
  else if (expectedState == signalledState) {
    // already signalled; don't wait
    complete_operation(opAddr);
    return;
  }
  else if (expectedState == cancelledState) {
    // cancelled before starting; clean up then complete

    // leave it in the no-waiter state
    clear_cancelled_state(state_, cancelledState);

    complete_operation(opAddr);
    return;
  }
  else {
    // someone is already waiting
    std::terminate();
  }
}

} // namespace unfiex::_unnamed
