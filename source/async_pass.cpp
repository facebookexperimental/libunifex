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

#if __cplusplus >= 201911L

#  include <unifex/async_pass.hpp>

namespace unifex::_pass {

accept_op_base_noargs* async_pass_base::try_claim_acceptor() noexcept {
  auto s = state_.load(std::memory_order_acquire);
  while (is_acceptor(s)) {
    if (state_.compare_exchange_weak(
            s, 0, std::memory_order_acq_rel, std::memory_order_acquire)) {
      return as_acceptor(s);
    }
  }
  return nullptr;
}

uintptr_t async_pass_base::try_claim_caller_raw() noexcept {
  auto s = state_.load(std::memory_order_acquire);
  while (is_caller(s)) {
    if (state_.compare_exchange_weak(
            s, 0, std::memory_order_acq_rel, std::memory_order_acquire)) {
      return s;
    }
  }
  return 0;
}

uintptr_t async_pass_base::call_or_suspend_raw(uintptr_t caller) noexcept {
  auto s = state_.load(std::memory_order_acquire);
  while (true) {
    if (is_acceptor(s)) {
      if (state_.compare_exchange_weak(
              s, 0, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return s;
      }
    } else if (s == 0) {
      if (state_.compare_exchange_weak(
              s,
              caller,
              std::memory_order_release,
              std::memory_order_acquire)) {
        return 0;
      }
    } else {
      std::terminate();
    }
  }
}

uintptr_t async_pass_base::accept_or_suspend_raw(uintptr_t acceptor) noexcept {
  auto s = state_.load(std::memory_order_acquire);
  while (true) {
    if (is_caller(s)) {
      if (state_.compare_exchange_weak(
              s, 0, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return s;
      }
    } else if (s == 0) {
      if (state_.compare_exchange_weak(
              s,
              acceptor,
              std::memory_order_release,
              std::memory_order_acquire)) {
        return 0;
      }
    } else {
      std::terminate();
    }
  }
}

}  // namespace unifex::_pass

#endif  // __cplusplus >= 201911L
