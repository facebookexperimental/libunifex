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

#include <unifex/win32/detail/types.hpp>

#include <utility>

namespace unifex::win32 {

class safe_handle {
public:
    safe_handle() noexcept : handle_(nullptr) {}

    explicit safe_handle(handle_t h) noexcept : handle_(h) {}

    safe_handle(safe_handle&& h) noexcept : handle_(std::exchange(h.handle_, nullptr)) {}

    ~safe_handle() { reset(); }

    safe_handle(const safe_handle&) = delete;

    safe_handle& operator=(safe_handle h) noexcept {
        swap(h);
        return *this;
    }

    handle_t get() const noexcept { return handle_; }

    handle_t release() noexcept { return std::exchange(handle_, nullptr); }

    void reset() noexcept;

    void swap(safe_handle& other) noexcept {
        std::swap(handle_, other.handle_);
    }

    friend bool operator==(const safe_handle& a, const safe_handle& b) noexcept {
        return a.handle_ == b.handle_;
    }
    friend bool operator==(const safe_handle& a, handle_t b) noexcept {
        return a.handle_ == b;
    }
    friend bool operator==(handle_t a, const safe_handle& b) noexcept {
        return a == b.handle_;
    }

    friend bool operator!=(const safe_handle& a, const safe_handle& b) noexcept {
        return !(a == b);
    }
    friend bool operator!=(const safe_handle& a, handle_t b) noexcept {
        return !(a == b);
    }
    friend bool operator!=(handle_t a, const safe_handle& b) noexcept {
        return !(a == b);
    }

private:
    handle_t handle_;
};

} // namespace unifex::win32
