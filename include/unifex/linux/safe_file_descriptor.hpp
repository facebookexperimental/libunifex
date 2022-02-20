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

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace linuxos {

class safe_file_descriptor {
 public:
  safe_file_descriptor() noexcept : fd_(-1) {}

  explicit safe_file_descriptor(int fd) noexcept : fd_(fd) {}

  safe_file_descriptor(safe_file_descriptor&& other) noexcept
      : fd_(std::exchange(other.fd_, -1)) {}

  ~safe_file_descriptor() {
    if (valid()) {
      close();
    }
  }

  safe_file_descriptor& operator=(safe_file_descriptor other) noexcept {
    std::swap(fd_, other.fd_);
    return *this;
  }

  bool valid() const noexcept {
    return fd_ >= 0;
  }

  int get() const noexcept {
    return fd_;
  }

  void close() noexcept;

 private:
  int fd_;
};

} // namespace linuxos
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
