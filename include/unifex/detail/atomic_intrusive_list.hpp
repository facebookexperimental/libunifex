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
#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

// A tagged atomic word used as a list link: bit 0 is a per-link
// spinlock; the remaining bits encode a node* (or 0 for null).
// Node alignment must be >= 2 so valid pointers have bit 0 clear.
using atomic_intrusive_list_link = std::atomic<uintptr_t>;

// Intrusive node.  Items stored in the list inherit from this.
//
//   self  — back-pointer to the link word referencing this node.
//           nullptr when the node is not in any list.
//   rest  — forward link (tagged, lockable).
struct atomic_intrusive_list_node {
  using link = atomic_intrusive_list_link;

  std::atomic<link*> self{nullptr};
  link rest{0};
};

// ---- Lock helpers (shared across all list instantiations) ----

class atomic_intrusive_list_link_ops {
public:
  using node = atomic_intrusive_list_node;
  using link = atomic_intrusive_list_link;

protected:
  static constexpr uintptr_t lock_bit = 1;

  // TTAS spinlock on a link word.  Returns the value (without
  // lock bit).
  static uintptr_t lock(link& lk) noexcept;

  // Release a link's spinlock, storing a new unlocked value.
  static void unlock(link& lk, uintptr_t value) noexcept {
    UNIFEX_ASSERT((value & lock_bit) == 0);
    lk.store(value, std::memory_order_release);
  }

  // Try to lock a link while monitoring an atomic<link*> for
  // changes.  Returns true (with head_val set) if locked,
  // false if the monitored pointer changed (caller retries).
  static bool try_lock_checking(
      link& lk,
      std::atomic<link*>& monitored,
      link* expected,
      uintptr_t& head_val) noexcept;
};

// ---- List implementation (parameterised on Latch) ------------
//
// A concurrent intrusive singly-linked list with per-link
// spinlocks and a permanent sentinel node.
//
// Operations:
//   push_front  O(1)  locks head
//   push_back   O(1)  locks tail
//   pop_front   O(1)  locks head + first item's rest
//   try_remove  O(1)  locks predecessor + item's rest
//   drain_into  O(1)  locks head + tail; moves all items
//
// Lock ordering: predecessor before successor.
//
// Latch mode (Latch=true):
//   Adds a second sentinel node.  The list is "latched" when
//   head_ points to the latch sentinel instead of the normal
//   one.  push_front_unless_latched, latch_and_drain, unlatch,
//   and is_latched all serialise through head_'s spinlock, so
//   there is no window between latching and draining.
//
//   Intended for async_manual_reset_event where "latched"
//   means "signalled".

template <bool Latch>
class atomic_intrusive_list_impl : protected atomic_intrusive_list_link_ops {
protected:
  using node = atomic_intrusive_list_node;
  using link = atomic_intrusive_list_link;

  atomic_intrusive_list_impl() noexcept;
  ~atomic_intrusive_list_impl();

  atomic_intrusive_list_impl(const atomic_intrusive_list_impl&) = delete;
  atomic_intrusive_list_impl(atomic_intrusive_list_impl&&) = delete;
  atomic_intrusive_list_impl&
  operator=(const atomic_intrusive_list_impl&) = delete;
  atomic_intrusive_list_impl& operator=(atomic_intrusive_list_impl&&) = delete;

  bool is_sentinel(const node* n) const noexcept;

  void push_front_impl(node* item) noexcept;
  void push_back_impl(node* item) noexcept;
  node* pop_front_impl() noexcept;
  bool try_remove_impl(node* item) noexcept;
  void drain_into_impl(atomic_intrusive_list_impl& target) noexcept;

  [[nodiscard]] bool empty_impl() const noexcept {
    auto val = head_.load(std::memory_order_relaxed);
    return is_sentinel(reinterpret_cast<node*>(val & ~lock_bit));
  }

  // ---- Latch operations (Latch=true only) ----

  // Push to front unless latched.  Returns true if pushed.
  bool push_front_unless_latched_impl(node* item) noexcept;

  // Atomically latch + move all items into target.
  void latch_and_drain_impl(atomic_intrusive_list_impl& target) noexcept;

  // Clear the latch (only if latched and empty).
  void unlatch_impl() noexcept;

  [[nodiscard]] bool is_latched_impl() const noexcept;

  // ---- Data ----

  node sentinel_;

  struct empty_t {};
  // sentinel_latch_ only occupies space when Latch=true.
  // With Latch=false, EBO eliminates it.
  UNIFEX_NO_UNIQUE_ADDRESS std::conditional_t<Latch, node, empty_t>
      sentinel_latch_{};

  link head_;
};

// ---- Typed wrapper -------------------------------------------

template <typename Item, bool Latch = false>
class atomic_intrusive_list : atomic_intrusive_list_impl<Latch> {
  using base = atomic_intrusive_list_impl<Latch>;

  static_assert(
      std::is_base_of_v<atomic_intrusive_list_node, Item>,
      "Item must inherit from atomic_intrusive_list_node");
  static_assert(
      alignof(Item) >= 2,
      "Item alignment must be >= 2 for the tag-bit protocol");

public:
  void push_front(Item* item) noexcept { base::push_front_impl(item); }

  void push_back(Item* item) noexcept { base::push_back_impl(item); }

  [[nodiscard]] Item* pop_front() noexcept {
    return static_cast<Item*>(base::pop_front_impl());
  }

  [[nodiscard]] bool try_remove(Item* item) noexcept {
    return base::try_remove_impl(item);
  }

  void drain_into(atomic_intrusive_list& target) noexcept {
    base::drain_into_impl(target);
  }

  [[nodiscard]] bool empty() const noexcept { return base::empty_impl(); }

  // ---- Latch operations (available only when Latch=true) ----

  template <bool L = Latch, std::enable_if_t<L, int> = 0>
  bool push_front_unless_latched(Item* item) noexcept {
    return base::push_front_unless_latched_impl(item);
  }

  template <bool L = Latch, std::enable_if_t<L, int> = 0>
  void latch_and_drain(atomic_intrusive_list& target) noexcept {
    base::latch_and_drain_impl(target);
  }

  template <bool L = Latch, std::enable_if_t<L, int> = 0>
  void unlatch() noexcept {
    base::unlatch_impl();
  }

  template <bool L = Latch, std::enable_if_t<L, int> = 0>
  [[nodiscard]] bool is_latched() const noexcept {
    return base::is_latched_impl();
  }
};

// Explicit-instantiation declarations (definitions in .cpp).
extern template class atomic_intrusive_list_impl<false>;
extern template class atomic_intrusive_list_impl<true>;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
