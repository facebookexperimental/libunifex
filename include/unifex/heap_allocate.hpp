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
#pragma once

#include <unifex/config.hpp>
#include <unifex/get_allocator.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/scope_guard.hpp>

#include <memory>
#include <type_traits>

namespace unifex {

template <typename Operation, typename Allocator>
class heap_allocated_operation {
  using allocator_t = typename std::allocator_traits<Allocator>::template rebind_alloc<Operation>;

public:
  template<typename Sender, typename Receiver>
  explicit heap_allocated_operation(Sender&& s, Receiver&& r)
      : allocator_(get_allocator(r)) {
    using allocator_traits = std::allocator_traits<allocator_t>;
    Operation* op = allocator_traits::allocate(allocator_, 1);
    bool constructorSucceeded = false;
    scope_guard freeOnError = [&]() noexcept {
      if (!constructorSucceeded) {
        allocator_traits::deallocate(allocator_, op, 1);
      }
    };
    op_ = ::new (static_cast<void*>(op)) Operation(
      connect((Sender&&)s, (Receiver&&)r));
    constructorSucceeded = true;
  }

  ~heap_allocated_operation() {
    op_->~Operation();
    std::allocator_traits<allocator_t>::deallocate(allocator_, op_, 1);
  }

  friend void tag_invoke(tag_t<start>, heap_allocated_operation& op) noexcept {
      start(*op.op_);
  }

private:
  Operation *op_;
  UNIFEX_NO_UNIQUE_ADDRESS allocator_t allocator_;
};

template <typename Sender>
class heap_allocate_sender {
public:
  template<
    template<typename...> class Variant,
    template<typename...> class Tuple>
  using value_types = typename Sender::template value_types<Variant, Tuple>;

  template<template<typename...> class Variant>
  using error_types = typename Sender::template error_types<Variant>;

  template <typename Receiver>
  friend auto tag_invoke(
      tag_t<connect>, heap_allocate_sender&& s, Receiver&& r)
      -> heap_allocated_operation<operation_t<Sender, Receiver>,
                                  std::remove_cvref_t<get_allocator_t<Receiver>>> {
    return heap_allocated_operation<operation_t<Sender, Receiver>,
                                  std::remove_cvref_t<get_allocator_t<Receiver>>>{
        (Sender&&)s.sender_, (Receiver&&) r};
  }

  template <typename Receiver>
  friend auto tag_invoke(
      tag_t<connect>, heap_allocate_sender& s, Receiver&& r)
      -> heap_allocated_operation<operation_t<Sender&, Receiver>,
                                  std::remove_cvref_t<get_allocator_t<Receiver>>> {
    return heap_allocated_operation<operation_t<Sender&, Receiver>,
                                  std::remove_cvref_t<get_allocator_t<Receiver>>>{
        s.sender_, (Receiver&&) r};
  }

  template <typename Receiver>
  friend auto tag_invoke(
      tag_t<connect>, const heap_allocate_sender& s, Receiver&& r)
      -> heap_allocated_operation<operation_t<const Sender&, Receiver>,
                                  std::remove_cvref_t<get_allocator_t<Receiver>>> {
    return heap_allocated_operation<operation_t<const Sender&, Receiver>,
                                  std::remove_cvref_t<get_allocator_t<Receiver>>>{
        std::as_const(s.sender_), (Receiver&&) r};
  }

  Sender sender_;
};

template<typename Sender>
auto heap_allocate(Sender&& sender) -> heap_allocate_sender<std::decay_t<Sender>> {
  return heap_allocate_sender<std::decay_t<Sender>>{(Sender&&)sender};
}

} // namespace unifex
