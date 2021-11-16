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

#include <functional>
#include <type_traits>

#include <unifex/detail/concept_macros.hpp>

#define UNIFEX_VTABLE_DECLARE                 \
  operator bool() { return ctx_ != nullptr; } \
  void* ctx_ = nullptr

#define UNIFEX_VTABLE_ENTRY(entry, ret, /*types*/...)   \
  using entry##_t = ret (*)(void*, __VA_ARGS__);        \
  template <typename... Args>                           \
  ret entry(Args&&... args) {                           \
    return entry##_(ctx_, std::forward<Args>(args)...); \
  }                                                     \
  entry##_t entry##_ = nullptr

#define UNIFEX_VTABLE_ENTRY_RVALUE(entry, ret, /*types*/...) \
  using entry##_t = ret (*)(void*, __VA_ARGS__);             \
  template <typename... Args>                                \
  ret entry(Args&&... args)&& {                              \
    return entry##_(ctx_, std::forward<Args>(args)...);      \
  }                                                          \
  entry##_t entry##_ = nullptr

#define UNIFEX_VTABLE_ENTRY_VOID(entry, ret) \
  using entry##_t = ret (*)(void*);          \
  ret entry() { return entry##_(ctx_); };    \
  entry##_t entry##_ = nullptr

#define UNIFEX_VTABLE_ENTRY_VOID_RVALUE(entry, ret) \
  using entry##_t = ret (*)(void*);                 \
  ret entry()&& { return entry##_(ctx_); };         \
  entry##_t entry##_ = nullptr

#define UNIFEX_VTABLE_CONSTRUCT_FN(fn) \
  (unifex::_vtable::                   \
       _construct_indirect<std::remove_reference_t<decltype(*this)>, fn>(fn)),

#define UNIFEX_VTABLE_CONSTRUCT(/*functions*/...) \
  { this, UNIFEX_PP_FOR_EACH(UNIFEX_VTABLE_CONSTRUCT_FN, __VA_ARGS__) }

namespace unifex {
namespace _vtable {

template <typename T, auto Key, typename Ret, typename... Args>
static constexpr auto _construct_indirect(Ret (T::*)(Args...)) noexcept {
  constexpr auto f = [](void* ctx, Args... args) {
    return std::invoke(Key, static_cast<T*>(ctx), std::forward<Args>(args)...);
  };
  return f;
}

template <typename T, auto Key, typename Ret, typename... Args>
static constexpr auto _construct_indirect(Ret (T::*)(Args...) &&) noexcept {
  constexpr auto f = [](void* ctx, Args... args) {
    return std::invoke(
        Key,
        static_cast<T&&>(*static_cast<T*>(ctx)),
        std::forward<Args>(args)...);
  };
  return f;
}

template <typename T, auto Key, typename Ret>
static constexpr auto _construct_indirect(Ret (T::*)()) noexcept {
  constexpr auto f = [](void* ctx) {
    return std::invoke(Key, static_cast<T*>(ctx));
  };
  return f;
}

template <typename T, auto Key, typename Ret>
static constexpr auto _construct_indirect(Ret (T::*)() &&) noexcept {
  constexpr auto f = [](void* ctx) {
    return std::invoke(Key, static_cast<T&&>(*static_cast<T*>(ctx)));
  };
  return f;
}

}  // namespace _vtable
}  // namespace unifex
