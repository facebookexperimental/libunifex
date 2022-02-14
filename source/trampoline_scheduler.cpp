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
#include <unifex/trampoline_scheduler.hpp>

namespace unifex {

thread_local trampoline_scheduler::trampoline_state*
    trampoline_scheduler::trampoline_state::current_ = nullptr;

void trampoline_scheduler::trampoline_state::drain() noexcept {
  while (head_ != nullptr) {
    operation_base* op = head_;
    head_ = op->next_;
    recursionDepth_ = 1;
    op->execute();
  }
}

} // namespace unifex
