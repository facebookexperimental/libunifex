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

#include <unifex/type_traits.hpp>

#include <type_traits>
#include <functional>
#include <memory>
#include <new>

namespace unifex {

template <typename T>
class manual_lifetime {
 public:
  manual_lifetime() noexcept {}
  ~manual_lifetime() {}

  template <typename... Args>
  T& construct(Args&&... args) noexcept(
      std::is_nothrow_constructible_v<T, Args...>) {
    return *::new (static_cast<void*>(std::addressof(value_)))
        T((Args &&) args...);
  }

  template <typename Func>
  T& construct_from(Func&& func) noexcept(noexcept(T(((Func &&) func)()))) {
    static_assert(
        std::is_same_v<callable_result_t<Func>, T>,
        "Return type of func() must be exactly T to permit copy-elision.");
    return *::new (static_cast<void*>(std::addressof(value_)))
        T(((Func &&) func)());
  }

  void destruct() noexcept(std::is_nothrow_destructible_v<T>) {
    value_.~T();
  }

  T& get() & noexcept {
    return value_;
  }
  T&& get() && noexcept {
    return (T &&) value_;
  }
  const T& get() const& noexcept {
    return value_;
  }
  const T&& get() const&& noexcept {
    return (const T&&)value_;
  }

 private:
  union {
    T value_;
  };
};

template <typename T>
class manual_lifetime<T&> {
 public:
  manual_lifetime() noexcept : value_(nullptr) {}
  ~manual_lifetime() {}

  T& construct(T& value) noexcept {
    value_ = std::addressof(value);
    return value;
  }

  template <typename Func>
  T& construct_from(Func&& func) noexcept(noexcept(((Func &&) func)())) {
    static_assert(std::is_same_v<callable_result_t<Func>, T&>);
    value_ = std::addressof(((Func &&) func)());
    return value_;
  }

  void destruct() noexcept {}

  T& get() const noexcept {
    return *value_;
  }

 private:
  T* value_;
};

template <typename T>
class manual_lifetime<T&&> {
 public:
  manual_lifetime() noexcept : value_(nullptr) {}
  ~manual_lifetime() {}

  T&& construct(T&& value) noexcept {
    value_ = std::addressof(value);
    return (T &&) value;
  }

  template <typename Func>
  T&& construct_from(Func&& func) noexcept(noexcept(((Func &&) func)())) {
    static_assert(std::is_same_v<callable_result_t<Func>, T&&>);
    value_ = std::addressof(((Func &&) func)());
    return (T &&) value_;
  }

  void destruct() noexcept {}

  T&& get() const noexcept {
    return (T &&) * value_;
  }

 private:
  T* value_;
};

template <>
class manual_lifetime<void> {
 public:
  manual_lifetime() noexcept = default;
  ~manual_lifetime() = default;

  void construct() noexcept {}
  template <typename Func>
  void construct_from(Func&& func) noexcept(noexcept(((Func &&) func)())) {
    static_assert(std::is_void_v<callable_result_t<Func>>);
    ((Func &&) func)();
  }
  void destruct() noexcept {}
  void get() const noexcept {}
};

} // namespace unifex
