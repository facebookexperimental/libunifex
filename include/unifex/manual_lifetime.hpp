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

#include <unifex/scope_guard.hpp>
#include <unifex/type_traits.hpp>

#include <type_traits>
#include <functional>
#include <memory>
#include <new>

#include <unifex/detail/prologue.hpp>

namespace unifex {

template <typename T>
class manual_lifetime {
  static_assert(is_nothrow_destructible_v<T>);
 public:
  manual_lifetime() noexcept {}
  ~manual_lifetime() {}

  template <typename... Args>
  [[maybe_unused]] T& construct(Args&&... args) noexcept(
      is_nothrow_constructible_v<T, Args...>) {
    return *::new (static_cast<void*>(std::addressof(value_)))
        T((Args &&) args...);
  }

  template <typename Func>
  [[maybe_unused]] T& construct_with(Func&& func) noexcept(is_nothrow_callable_v<Func>) {
    static_assert(
        is_same_v<callable_result_t<Func>, T>,
        "Return type of func() must be exactly T to permit copy-elision.");
    return *::new (static_cast<void*>(std::addressof(value_)))
        T(((Func &&) func)());
  }

  void destruct() noexcept {
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

  [[maybe_unused]] T& construct(T& value) noexcept {
    value_ = std::addressof(value);
    return value;
  }

  template <typename Func>
  [[maybe_unused]] T& construct_with(Func&& func) noexcept(is_nothrow_callable_v<Func>) {
    static_assert(is_same_v<callable_result_t<Func>, T&>);
    value_ = std::addressof(((Func &&) func)());
    return get();
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

  [[maybe_unused]] T&& construct(T&& value) noexcept {
    value_ = std::addressof(value);
    return (T &&) value;
  }

  template <typename Func>
  [[maybe_unused]] T&& construct_with(Func&& func) noexcept(is_nothrow_callable_v<Func>) {
    static_assert(is_same_v<callable_result_t<Func>, T&&>);
    value_ = std::addressof(((Func &&) func)());
    return get();
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
  void construct_with(Func&& func) noexcept(is_nothrow_callable_v<Func>) {
    static_assert(is_void_v<callable_result_t<Func>>);
    ((Func &&) func)();
  }
  void destruct() noexcept {}
  void get() const noexcept {}
};

template <>
class manual_lifetime<void const> : public manual_lifetime<void> {};

// For activating a manual_lifetime when it is in a union and initializing
// its value from arguments to its constructor.
template <typename T, typename... Args>
[[maybe_unused]] //
T& activate_union_member(manual_lifetime<T>& box, Args&&... args) noexcept(
    is_nothrow_constructible_v<T, Args...>) {
  auto* p = ::new (&box) manual_lifetime<T>{};
  scope_guard guard = [=]() noexcept { p->~manual_lifetime(); };
  auto& t = box.construct(static_cast<Args&&>(args)...);
  guard.release();
  return t;
}

inline void activate_union_member(manual_lifetime<void>& box) noexcept {
  (::new (&box) manual_lifetime<void>{})->construct();
}

// For activating a manual_lifetime when it is in a union and initializing
// its value from the result of calling a function.
template <typename T, typename Func>
[[maybe_unused]] //
T& activate_union_member_with(manual_lifetime<T>& box, Func&& func) noexcept(
    is_nothrow_callable_v<Func>) {
  auto* p = ::new (&box) manual_lifetime<T>{};
  scope_guard guard = [=]() noexcept { p->~manual_lifetime(); };
  auto& t = p->construct_with(static_cast<Func&&>(func));
  guard.release();
  return t;
}

// For deactivating a manual_lifetime when it is in a union
template <typename T>
void deactivate_union_member(manual_lifetime<T>& box) noexcept {
  box.destruct();
  box.~manual_lifetime();
}

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
