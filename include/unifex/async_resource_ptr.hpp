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

#include <unifex/config.hpp>
#include <unifex/async_manual_reset_event.hpp>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace unifex {
namespace _async_resource_ptr {

template <typename Resource>
struct [[nodiscard]] async_resource_ptr final {
  template <typename R>
  friend struct async_resource_ptr;

  // @deprecated transitional adapter for v2/detached_spawn
  explicit async_resource_ptr(std::unique_ptr<Resource> resource) noexcept
    : resource_(resource.release())
    , evt_(nullptr) {
    UNIFEX_ASSERT(resource_ != nullptr);
    UNIFEX_ASSERT(evt_ == nullptr);
  }

  async_resource_ptr(
      Resource* resource, unifex::async_manual_reset_event* evt) noexcept
    : resource_(resource)
    , evt_(evt) {
    UNIFEX_ASSERT(resource_ != nullptr);
    UNIFEX_ASSERT(evt_ != nullptr);
  }

  template <
      typename Resource2,
      std::enable_if_t<
          !std::is_same_v<Resource, Resource2> &&
          std::is_convertible_v<Resource2*, Resource*>>* = nullptr>
  /* implicit */ async_resource_ptr(async_resource_ptr<Resource2> h) noexcept
    : resource_(std::exchange(h.resource_, nullptr))
    , evt_(std::exchange(h.evt_, nullptr)) {}

  async_resource_ptr(async_resource_ptr&& h) noexcept
    : resource_(std::exchange(h.resource_, nullptr))
    , evt_(std::exchange(h.evt_, nullptr)) {}

  async_resource_ptr(const async_resource_ptr& h) noexcept = delete;

  async_resource_ptr() noexcept : resource_(nullptr), evt_(nullptr) {}

  /* implicit */ async_resource_ptr(std::nullptr_t) noexcept
    : resource_(nullptr)
    , evt_(nullptr) {}

  ~async_resource_ptr() noexcept { reset(); }

  Resource* operator->() const noexcept { return resource_; }

  Resource& operator*() const noexcept { return *resource_; }

  async_resource_ptr& operator=(async_resource_ptr h) noexcept {
    swap(h);
    return *this;
  }

  explicit operator bool() const noexcept { return resource_ != nullptr; }

  Resource* get() const noexcept { return resource_; }

  void reset() noexcept {
    if (evt_) {
      evt_->set();
      evt_ = nullptr;
    } else {
      delete resource_;
    }
    resource_ = nullptr;
  }

  void swap(async_resource_ptr& other) noexcept {
    std::swap(resource_, other.resource_);
    std::swap(evt_, other.evt_);
  }

  friend bool operator==(
      const async_resource_ptr& a, const async_resource_ptr& b) noexcept {
    return a.resource_ == b.resource_ && a.evt_ == b.evt_;
  }

  friend bool operator!=(
      const async_resource_ptr& a, const async_resource_ptr& b) noexcept {
    return !(a == b);
  }

private:
  Resource* resource_;
  unifex::async_manual_reset_event* evt_;
};
}  // namespace _async_resource_ptr
using _async_resource_ptr::async_resource_ptr;
}  // namespace unifex

namespace std {
template <typename Resource>
struct hash<unifex::async_resource_ptr<Resource>> {
  std::size_t
  operator()(const unifex::async_resource_ptr<Resource>& p) noexcept {
    return std::hash<Resource*>{}(p.get());
  }
};
}  // namespace std
