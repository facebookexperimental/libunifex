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

#include <exception>
#include <utility>
#include <type_traits>

#include <unifex/std_concepts.hpp>

#include <unifex/detail/prologue.hpp>

// Microsoft's STL has a fast implementation of std::make_exception_ptr.
// So does libc++ when targetting Microsoft's ABI. On other stdlibs,
// move as much exception machinery into a cpp file as possible to avoid
// template code bloat.
#if !defined(_LIBCPP_ABI_MICROSOFT) && !defined(_MSVC_STL_VERSION)
#define UNIFEX_HAS_FAST_MAKE_EXCEPTION_PTR 0
#else
#define UNIFEX_HAS_FAST_MAKE_EXCEPTION_PTR 1
#endif

namespace unifex {
namespace _throw {
struct _fn {
  template <typename Exception>
  [[noreturn]] UNIFEX_ALWAYS_INLINE
  void operator()([[maybe_unused]] Exception&& ex) const {
  #if !UNIFEX_NO_EXCEPTIONS
    throw (Exception&&) ex;
  #else
    std::terminate();
  #endif
  }
};
} // namespace _throw
inline constexpr _throw::_fn throw_ {};

namespace _except_ptr {

#if !UNIFEX_HAS_FAST_MAKE_EXCEPTION_PTR
// If std::make_exeption_ptr() is slow, then move it into a cpp
// file to generate less code elsewhere.
struct _ref {
  template (typename Obj)
    (requires (!same_as<remove_cvref_t<Obj>, _ref>))
  _ref(Obj&& obj) noexcept
    : p_((void*) std::addressof(obj))
    , throw_([](void* p) {
        unifex::throw_((Obj&&) *(std::add_pointer_t<Obj>) p);
      })
  {}
  _ref(_ref&&) = delete;
  [[noreturn]] void rethrow() const;
private:
  void* p_;
  void (*throw_)(void*);
};

struct _fn {
  [[nodiscard]] std::exception_ptr operator()(_ref) const noexcept;
};
#else // !UNIFEX_HAS_FAST_MAKE_EXCEPTION_PTR
struct _fn {
  template <typename Obj>
  [[nodiscard]] UNIFEX_ALWAYS_INLINE
  std::exception_ptr operator()(Obj&& obj) const noexcept {
      return std::make_exception_ptr((Obj&&) obj);
  }
};
#endif

} // namespace _except_ptr

inline constexpr _except_ptr::_fn make_exception_ptr {};
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
