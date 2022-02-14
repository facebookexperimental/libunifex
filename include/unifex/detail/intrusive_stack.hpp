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

#include <utility>

namespace unifex {

template<typename T, T* T::*Next>
class intrusive_stack {
public:
    intrusive_stack() : head_(nullptr) {}

    intrusive_stack(intrusive_stack&& other) noexcept : head_(std::exchange(other.head_, nullptr)) {}

    intrusive_stack(const intrusive_stack&) = delete;
    intrusive_stack& operator=(const intrusive_stack&) = delete;
    intrusive_stack& operator=(intrusive_stack&&) = delete;

    ~intrusive_stack() {
        UNIFEX_ASSERT(empty());
    }

    // Adopt an existing linked-list as a stack.
    static intrusive_stack adopt(T* head) noexcept {
        intrusive_stack stack;
        stack.head_ = head;
        return stack;
    }

    T* release() noexcept {
        return std::exchange(head_, nullptr);
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_ == nullptr;
    }

    void push_front(T* item) noexcept {
        item->*Next = head_;
        head_ = item;
    }

    [[nodiscard]] T* pop_front() noexcept {
        UNIFEX_ASSERT(!empty());
        T* item = head_;
        head_ = item->*Next;
        return item;
    }

private:
    T* head_;
};

} // namespace unifex
