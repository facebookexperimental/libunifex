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
#pragma once


#include <unifex/detail/prologue.hpp>

namespace unifex {

// A doubly-linked intrusive list maintained in ascending order of the 'SortKey'
// field of the list items.
template <typename T, T* T::*Next, T* T::*Prev, typename Key, Key T::*SortKey>
class intrusive_heap {
 public:
  intrusive_heap() noexcept : head_(nullptr) {}

  ~intrusive_heap() {
    T* item = head_;
    if (item != nullptr) {
      UNIFEX_ASSERT(item->*Prev == nullptr);
    }
    while (item != nullptr) {
      if (item->*Next != nullptr) {
        UNIFEX_ASSERT(item->*Next->*Prev == item);
      }
      item = item->*Next;
    }

    UNIFEX_ASSERT(empty());
  }

  bool empty() const noexcept {
    return head_ == nullptr;
  }

  T* top() const noexcept {
    UNIFEX_ASSERT(!empty());
    return head_;
  }

  T* pop() noexcept {
    UNIFEX_ASSERT(!empty());
    T* item = head_;
    head_ = item->*Next;
    if (head_ != nullptr) {
      head_->*Prev = nullptr;
    }
    return item;
  }

  void insert(T* item) noexcept {
    // Simple insertion sort to insert item in the right place in the list
    // to keep the list sorted by 'item->*SortKey'.
    // TODO: Replace this with a non-toy data-structure.
    if (head_ == nullptr) {
      head_ = item;
      item->*Next = nullptr;
      item->*Prev = nullptr;
    } else if (item->*SortKey < head_->*SortKey) {
      item->*Next = head_;
      item->*Prev = nullptr;
      head_->*Prev = item;
      head_ = item;
    } else {
      auto* insertAfter = head_;
      while (insertAfter->*Next != nullptr &&
             insertAfter->*Next->*SortKey <= item->*SortKey) {
        insertAfter = insertAfter->*Next;
      }

      auto* insertBefore = insertAfter->*Next;

      item->*Prev = insertAfter;
      item->*Next = insertBefore;
      insertAfter->*Next = item;
      if (insertBefore != nullptr) {
        insertBefore->*Prev = item;
      }
    }
  }

  void remove(T* item) noexcept {
    auto* prev = item->*Prev;
    auto* next = item->*Next;
    if (prev != nullptr) {
      prev->*Next = next;
    } else {
      UNIFEX_ASSERT(head_ == item);
      head_ = next;
    }
    if (next != nullptr) {
      next->*Prev = prev;
    }
  }

 private:
  T* head_;
};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
