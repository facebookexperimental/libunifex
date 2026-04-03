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

#include <unifex/detail/atomic_intrusive_list.hpp>

namespace unifex {

using node = atomic_intrusive_list_node;
using link = atomic_intrusive_list_link;

namespace {

node* to_node(uintptr_t v) noexcept {
  return reinterpret_cast<node*>(v);
}

uintptr_t to_value(node* p) noexcept {
  return reinterpret_cast<uintptr_t>(p);
}

}  // namespace

// ---- Lock helpers (non-template) ----

uintptr_t atomic_intrusive_list_link_ops::lock(link& lk) noexcept {
  uintptr_t val = lk.load(std::memory_order_relaxed);
  while (true) {
    while (val & lock_bit) {
      val = lk.load(std::memory_order_relaxed);
    }
    if (lk.compare_exchange_weak(
            val,
            val | lock_bit,
            std::memory_order_acquire,
            std::memory_order_relaxed)) {
      return val;
    }
  }
}

bool atomic_intrusive_list_link_ops::try_lock_checking(
    link& lk,
    std::atomic<link*>& monitored,
    link* expected,
    uintptr_t& head_val) noexcept {
  {
    link* cur = monitored.load(std::memory_order_acquire);
    if (cur != expected) {
      return false;
    }
  }

  uintptr_t val = lk.load(std::memory_order_relaxed);
  while (true) {
    if (val & lock_bit) {
      link* cur = monitored.load(std::memory_order_acquire);
      if (cur != expected) {
        return false;
      }
      val = lk.load(std::memory_order_relaxed);
      continue;
    }

    // Acquire fence synchronises with the release that wrote
    // this unlocked value, making any monitored-pointer update
    // sequenced before that release visible.
    std::atomic_thread_fence(std::memory_order_acquire);

    link* cur = monitored.load(std::memory_order_acquire);
    if (cur != expected) {
      return false;
    }

    if (lk.compare_exchange_weak(
            val,
            val | lock_bit,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
      head_val = val;
      return true;
    }
  }
}

// ---- List implementation (template) ----

template <bool Latch>
atomic_intrusive_list_impl<Latch>::atomic_intrusive_list_impl() noexcept {
  head_.store(to_value(&sentinel_), std::memory_order_relaxed);
  sentinel_.self.store(&head_, std::memory_order_relaxed);
}

template <bool Latch>
atomic_intrusive_list_impl<Latch>::~atomic_intrusive_list_impl() {
  UNIFEX_ASSERT(is_sentinel(to_node(head_.load(std::memory_order_relaxed))));
}

template <bool Latch>
bool atomic_intrusive_list_impl<Latch>::is_sentinel(
    const node* n) const noexcept {
  if constexpr (Latch) {
    return n == &sentinel_ || n == &sentinel_latch_;
  } else {
    return n == &sentinel_;
  }
}

template <bool Latch>
void atomic_intrusive_list_impl<Latch>::push_front_impl(node* item) noexcept {
  UNIFEX_ASSERT(item != nullptr);
  UNIFEX_ASSERT(item->self.load(std::memory_order_relaxed) == nullptr);

  uintptr_t old_head = lock(head_);
  node* old_first = to_node(old_head);
  UNIFEX_ASSERT(old_first != nullptr);

  item->rest.store(old_head, std::memory_order_relaxed);
  old_first->self.store(&item->rest, std::memory_order_release);
  item->self.store(&head_, std::memory_order_release);

  unlock(head_, to_value(item));
}

template <bool Latch>
void atomic_intrusive_list_impl<Latch>::push_back_impl(node* item) noexcept {
  UNIFEX_ASSERT(item != nullptr);
  UNIFEX_ASSERT(item->self.load(std::memory_order_relaxed) == nullptr);

  item->rest.store(to_value(&sentinel_), std::memory_order_relaxed);

  while (true) {
    link* pred_link = sentinel_.self.load(std::memory_order_acquire);

    uintptr_t pred_val;
    if (!try_lock_checking(*pred_link, sentinel_.self, pred_link, pred_val)) {
      continue;
    }
    UNIFEX_ASSERT(pred_val == to_value(&sentinel_));

    item->self.store(pred_link, std::memory_order_release);
    sentinel_.self.store(&item->rest, std::memory_order_release);

    unlock(*pred_link, to_value(item));
    return;
  }
}

template <bool Latch>
node* atomic_intrusive_list_impl<Latch>::pop_front_impl() noexcept {
  uintptr_t old_head = lock(head_);
  node* first = to_node(old_head);

  if (is_sentinel(first)) {
    unlock(head_, old_head);
    return nullptr;
  }

  uintptr_t rest_val = lock(first->rest);
  node* second = to_node(rest_val);
  UNIFEX_ASSERT(second != nullptr);

  second->self.store(&head_, std::memory_order_release);
  first->self.store(nullptr, std::memory_order_relaxed);

  unlock(head_, rest_val);
  unlock(first->rest, 0);
  return first;
}

template <bool Latch>
bool atomic_intrusive_list_impl<Latch>::try_remove_impl(node* item) noexcept {
  UNIFEX_ASSERT(item != nullptr);

  while (true) {
    link* head_ptr = item->self.load(std::memory_order_acquire);
    if (!head_ptr) {
      return false;
    }

    uintptr_t head_val;
    if (!try_lock_checking(*head_ptr, item->self, head_ptr, head_val)) {
      continue;
    }

    link* cur_self = item->self.load(std::memory_order_acquire);
    if (cur_self != head_ptr) {
      unlock(*head_ptr, head_val);
      if (!cur_self) {
        return false;
      }
      continue;
    }
    UNIFEX_ASSERT(to_node(head_val) == item);

    uintptr_t rest_val = lock(item->rest);
    node* successor = to_node(rest_val);
    UNIFEX_ASSERT(successor != nullptr);

    successor->self.store(head_ptr, std::memory_order_release);
    item->self.store(nullptr, std::memory_order_relaxed);

    unlock(*head_ptr, rest_val);
    unlock(item->rest, 0);
    return true;
  }
}

template <bool Latch>
void atomic_intrusive_list_impl<Latch>::drain_into_impl(
    atomic_intrusive_list_impl& target) noexcept {
  UNIFEX_ASSERT(&target != this);
  UNIFEX_ASSERT(target.empty_impl());

  uintptr_t old_head = lock(head_);
  node* first = to_node(old_head);

  if (is_sentinel(first)) {
    unlock(head_, old_head);
    return;
  }

  // Lock the sentinel predecessor (last real node).
  link* pred_link;
  uintptr_t pred_val;
  while (true) {
    pred_link = sentinel_.self.load(std::memory_order_acquire);
    if (try_lock_checking(*pred_link, sentinel_.self, pred_link, pred_val)) {
      break;
    }
  }
  UNIFEX_ASSERT(pred_val == to_value(&sentinel_));

  // Retarget source sentinel to head before unlocking, so a
  // concurrent push_back targets head_ (which we still hold).
  sentinel_.self.store(&head_, std::memory_order_release);

  // Splice the chain into the target.
  target.sentinel_.self.store(pred_link, std::memory_order_release);
  target.head_.store(old_head, std::memory_order_relaxed);
  first->self.store(&target.head_, std::memory_order_release);

  unlock(*pred_link, to_value(&target.sentinel_));
  unlock(head_, to_value(&sentinel_));
}

// ---- Latch operations ----
//
// Only meaningful when Latch=true.  The if-constexpr guards
// allow the class-level explicit instantiation below to
// compile for Latch=false without emitting any latch code.

template <bool Latch>
bool atomic_intrusive_list_impl<Latch>::push_front_unless_latched_impl(
    node* item) noexcept {
  if constexpr (Latch) {
    UNIFEX_ASSERT(item != nullptr);
    UNIFEX_ASSERT(item->self.load(std::memory_order_relaxed) == nullptr);

    uintptr_t old_head = lock(head_);
    node* old_first = to_node(old_head);

    if (old_first == &sentinel_latch_) {
      unlock(head_, old_head);
      return false;
    }

    UNIFEX_ASSERT(old_first != nullptr);
    item->rest.store(old_head, std::memory_order_relaxed);
    old_first->self.store(&item->rest, std::memory_order_release);
    item->self.store(&head_, std::memory_order_release);

    unlock(head_, to_value(item));
    return true;
  } else {
    (void)item;
    UNIFEX_ASSERT(false);
    return false;
  }
}

template <bool Latch>
void atomic_intrusive_list_impl<Latch>::latch_and_drain_impl(
    atomic_intrusive_list_impl& target) noexcept {
  if constexpr (Latch) {
    UNIFEX_ASSERT(&target != this);
    UNIFEX_ASSERT(target.empty_impl());

    uintptr_t old_head = lock(head_);
    node* first = to_node(old_head);

    if (first == &sentinel_latch_) {
      unlock(head_, old_head);
      return;
    }

    if (first == &sentinel_) {
      sentinel_.self.store(nullptr, std::memory_order_relaxed);
      sentinel_latch_.self.store(&head_, std::memory_order_release);
      unlock(head_, to_value(&sentinel_latch_));
      return;
    }

    // Has items — drain them into target, then latch.
    link* pred_link;
    uintptr_t pred_val;
    while (true) {
      pred_link = sentinel_.self.load(std::memory_order_acquire);
      if (try_lock_checking(*pred_link, sentinel_.self, pred_link, pred_val)) {
        break;
      }
    }
    UNIFEX_ASSERT(pred_val == to_value(&sentinel_));

    sentinel_.self.store(nullptr, std::memory_order_relaxed);
    sentinel_latch_.self.store(&head_, std::memory_order_release);

    target.sentinel_.self.store(pred_link, std::memory_order_release);
    target.head_.store(old_head, std::memory_order_relaxed);
    first->self.store(&target.head_, std::memory_order_release);

    unlock(*pred_link, to_value(&target.sentinel_));
    unlock(head_, to_value(&sentinel_latch_));
  } else {
    (void)target;
    UNIFEX_ASSERT(false);
  }
}

template <bool Latch>
void atomic_intrusive_list_impl<Latch>::unlatch_impl() noexcept {
  if constexpr (Latch) {
    uintptr_t old_head = lock(head_);
    node* first = to_node(old_head);

    if (first == &sentinel_latch_) {
      sentinel_latch_.self.store(nullptr, std::memory_order_relaxed);
      sentinel_.self.store(&head_, std::memory_order_release);
      unlock(head_, to_value(&sentinel_));
    } else {
      unlock(head_, old_head);
    }
  } else {
    UNIFEX_ASSERT(false);
  }
}

template <bool Latch>
bool atomic_intrusive_list_impl<Latch>::is_latched_impl() const noexcept {
  if constexpr (Latch) {
    auto val = head_.load(std::memory_order_acquire);
    return (val & ~lock_bit) == reinterpret_cast<uintptr_t>(&sentinel_latch_);
  } else {
    return false;
  }
}

// Explicit instantiations.
template class atomic_intrusive_list_impl<false>;
template class atomic_intrusive_list_impl<true>;

}  // namespace unifex
