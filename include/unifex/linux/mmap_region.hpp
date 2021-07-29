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

#include <cstdint>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace linuxos {

struct mmap_region {
  mmap_region() noexcept : ptr_(nullptr), size_(0) {}

  mmap_region(mmap_region&& r) noexcept
      : ptr_(std::exchange(r.ptr_, nullptr)),
        size_(std::exchange(r.size_, 0)) {}

  explicit mmap_region(void* ptr, std::size_t size) noexcept
      : ptr_(ptr), size_(size) {}

  ~mmap_region();

  mmap_region& operator=(mmap_region r) noexcept {
    std::swap(ptr_, r.ptr_);
    std::swap(size_, r.size_);
    return *this;
  }

  void* data() const noexcept {
    return ptr_;
  }

  std::size_t size() const noexcept {
    return size_;
  }

 private:
  void* ptr_;
  std::size_t size_;
};

} // namespace linuxos
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
