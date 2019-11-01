#pragma once

#include <cassert>
#include <tuple>
#include <utility>

namespace unifex {

template <typename Item, Item* Item::*Next>
class intrusive_queue {
 public:
  intrusive_queue() noexcept = default;

  intrusive_queue(intrusive_queue&& other) noexcept
      : head_(std::exchange(other.head_, nullptr)),
        tail_(std::exchange(other.tail_, nullptr)) {}

  intrusive_queue& operator=(intrusive_queue other) noexcept {
    std::swap(head_, other.head_);
    std::swap(tail_, other.tail_);
  }

  ~intrusive_queue() {
    assert(empty());
  }

  static intrusive_queue make_reversed(Item* list) noexcept {
    Item* newHead = nullptr;
    Item* newTail = list;
    while (list != nullptr) {
      Item* next = list->*Next;
      list->*Next = newHead;
      newHead = list;
      list = next;
    }

    intrusive_queue result;
    result.head_ = newHead;
    result.tail_ = newTail;
    return result;
  }

  [[nodiscard]] bool empty() const noexcept {
    return head_ == nullptr;
  }

  [[nodiscard]] Item* pop_front() noexcept {
    assert(!empty());
    Item* item = std::exchange(head_, head_->*Next);
    if (head_ == nullptr) {
      tail_ = nullptr;
    }
    return item;
  }

  void push_front(Item* item) noexcept {
    assert(item != nullptr);
    item->*Next = head_;
    head_ = item;
    if (tail_ == nullptr) {
      tail_ = item;
    }
  }

  void push_back(Item* item) noexcept {
    assert(item != nullptr);
    item->*Next = nullptr;
    if (tail_ == nullptr) {
      head_ = item;
    } else {
      tail_->*Next = item;
    }
    tail_ = item;
  }

  void append(intrusive_queue other) noexcept {
    if (other.empty())
      return;
    auto* otherHead = std::exchange(other.head_, nullptr);
    if (empty()) {
      head_ = otherHead;
    } else {
      tail_->*Next = otherHead;
    }
    tail_ = std::exchange(other.tail_, nullptr);
  }

  void prepend(intrusive_queue other) noexcept {
    if (other.empty())
      return;

    other.tail_->*Next = head_;
    head_ = other.head_;
    if (tail_ == nullptr) {
      tail_ = other.tail_;
    }

    other.tail_ = nullptr;
    other.head_ = nullptr;
  }

 private:
  Item* head_ = nullptr;
  Item* tail_ = nullptr;
};

} // namespace unifex
