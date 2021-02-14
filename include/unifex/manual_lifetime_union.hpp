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

#include <unifex/manual_lifetime.hpp>
#include <unifex/scope_guard.hpp>

#include <utility>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

template <typename... Ts>
class manual_lifetime_union {
 public:
  manual_lifetime_union() = default;

  template <typename T, typename... Args>
  [[maybe_unused]] T& construct(Args&&... args) noexcept(
      is_nothrow_constructible_v<T, Args...>) {
    return unifex::activate_union_member(
      *static_cast<manual_lifetime<T>*>(static_cast<void*>(&storage_)),
      static_cast<Args&&>(args)...);
  }
  template <typename T, typename Func>
  [[maybe_unused]] T& construct_with(Func&& func) noexcept(is_nothrow_callable_v<Func>) {
    return unifex::activate_union_member_with(
      *static_cast<manual_lifetime<T>*>(static_cast<void*>(&storage_)),
      static_cast<Func&&>(func));
  }
  template <typename T>
  void destruct() noexcept {
    unifex::deactivate_union_member(
      *static_cast<manual_lifetime<T>*>(static_cast<void*>(&storage_)));
  }

  template <typename T>
  decltype(auto) get() & noexcept {
    static_assert(is_one_of_v<T, Ts...>);
    return static_cast<manual_lifetime<T>*>(static_cast<void*>(&storage_))
        ->get();
  }
  template <typename T>
  decltype(auto) get() const & noexcept {
    static_assert(is_one_of_v<T, Ts...>);
    return static_cast<manual_lifetime<T> const*>(
        static_cast<void const*>(&storage_))->get();
  }
  template <typename T>
  decltype(auto) get() && noexcept {
    static_assert(is_one_of_v<T, Ts...>);
    return std::move(
      *static_cast<manual_lifetime<T>*>(static_cast<void*>(&storage_))).get();
  }

 private:
  std::aligned_union_t<0, manual_lifetime<Ts>...> storage_;
};

template <>
class manual_lifetime_union<> {};

// For activating a manual_lifetime_union when it is in a union and initializing
// its value from arguments to its constructor.
template <typename T, typename... Ts, typename... Args>
[[maybe_unused]] //
T& activate_union_member(manual_lifetime_union<Ts...>& box, Args&&... args) //
    noexcept(is_nothrow_constructible_v<T, Args...>) {
  auto* p = ::new (&box) manual_lifetime_union<Ts...>{};
  scope_guard guard = [=]() noexcept { p->~manual_lifetime_union(); };
  auto& t = p->template construct<T>(static_cast<Args&&>(args)...);
  guard.release();
  return t;
}

// For activating a manual_lifetime_union when it is in a union and initializing
// its value from the result of calling a function.
template <typename T, typename... Ts, typename Func>
[[maybe_unused]] //
T& activate_union_member_with(manual_lifetime_union<Ts...>& box, Func&& func)
    noexcept(is_nothrow_callable_v<Func>) {
  auto* p = ::new (&box) manual_lifetime_union<Ts...>{};
  scope_guard guard = [=]() noexcept { p->~manual_lifetime_union(); };
  auto& t = p->template construct_with<T>(static_cast<Func&&>(func));
  guard.release();
  return t;
}

// For deactivating a manual_lifetime_union when it is in a union
template <typename T, typename... Ts>
void deactivate_union_member(manual_lifetime_union<Ts...>& box) noexcept {
  box.template destruct<T>();
  box.~manual_lifetime_union();
}

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
