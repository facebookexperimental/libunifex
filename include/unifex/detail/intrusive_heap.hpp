#pragma once

#include <cassert>

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
      assert(item->*Prev == nullptr);
    }
    while (item != nullptr) {
      std::printf("- %p\n", item);
      if (item->*Next != nullptr) {
        assert(item->*Next->*Prev == item);
      }
      item = item->*Next;
    }

    assert(empty());
  }

  bool empty() const noexcept {
    return head_ == nullptr;
  }

  T* top() const noexcept {
    assert(!empty());
    return head_;
  }

  T* pop() noexcept {
    assert(!empty());
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
      assert(head_ == item);
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
