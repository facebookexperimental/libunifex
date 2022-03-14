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
#include <unifex/type_traits.hpp>

#if UNIFEX_NO_RTTI
#include <functional>
#else
#include <typeindex>
#endif

#include <unifex/detail/prologue.hpp>

namespace unifex {

#if UNIFEX_NO_RTTI

struct type_index final {
  const char* name() const noexcept {
    return index_;
  }

  std::size_t hash_code() const noexcept {
    return std::hash<const void*>{}(index_);
  }

 private:
  const char* index_;

  explicit type_index(const char* index) noexcept
    : index_(index) {}

  friend bool operator==(type_index lhs, type_index rhs) noexcept {
    return lhs.index_ == rhs.index_;
  }

  friend bool operator!=(type_index lhs, type_index rhs) noexcept {
    return !(lhs == rhs);
  }

  friend bool operator<(type_index lhs, type_index rhs) noexcept {
    return std::less<const void*>{}(lhs.index_, rhs.index_);
  }

  friend bool operator>(type_index lhs, type_index rhs) noexcept {
    return rhs < lhs;
  }

  friend bool operator<=(type_index lhs, type_index rhs) noexcept {
    return !(lhs > rhs);
  }

  friend bool operator>=(type_index lhs, type_index rhs) noexcept {
    return !(rhs < lhs);
  }

  template <typename T>
  friend type_index type_id() noexcept;

  template <typename T>
  static type_index make() noexcept {
    static_assert(std::is_same_v<T, remove_cvref_t<T>>);

#ifdef __FUNCSIG__
    static constexpr auto index = __FUNCSIG__;
#else
    static constexpr auto index = __PRETTY_FUNCTION__;
#endif

    return type_index{index};
  }
};

template <typename T>
type_index type_id() noexcept {
  return type_index::make<remove_cvref_t<T>>();
}

#else

using type_index = std::type_index;

template <typename T>
type_index type_id() noexcept {
  return typeid(T);
}

#endif

} // namespace unifex

#if UNIFEX_NO_RTTI

namespace std {

template <>
struct hash<unifex::type_index> {
  std::size_t operator()(unifex::type_index index) const noexcept {
    return index.hash_code();
  }
};

} // namespace std

#endif

#include <unifex/detail/epilogue.hpp>
