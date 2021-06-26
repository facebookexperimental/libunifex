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

#include <unifex/this.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

//
// This header contains some helper-CPOs for type-erasing wrappers that let us
// create vtable entries for built-in operations like destructors and
// copy/move constructors.
//
// These CPOs can be passed to the list of CPOs for a vtable<> to create vtable
// entries for these operations. These CPOs are customisable by defining the
// corresponding special member function instead of using tag_invoke().
// e.g. _destroy_cpo is customised by defining a destructor.
//      _move_construct_cpo is customised by defining a move-constructor.
//      _copy_construct_cpo is customsied by defining a copy-constructor.
//

namespace unifex
{
  namespace detail
  {
    struct _destroy_cpo {
      using type_erased_signature_t = void(this_&) noexcept;

      template(typename T)                              //
          (requires std::is_nothrow_destructible_v<T>)  //
          void
          operator()(T& object) const noexcept {
        object.~T();
      }
    };

    template <bool RequireNoexceptMove>
    struct _move_construct_cpo {
      using type_erased_signature_t =
          void(void* p, this_&& src) noexcept(RequireNoexceptMove);

      template(typename T)  //
          (requires(
              !RequireNoexceptMove ||
              std::is_nothrow_move_constructible_v<T>))  //
          void
          operator()(void* p, T&& src) const
          noexcept(std::is_nothrow_move_constructible_v<T>) {
        ::new (p) T(static_cast<T&&>(src));
      }
    };

    template <bool RequireNoexceptCopy>
    struct _copy_construct_cpo {
      using type_erased_signature_t =
          void(void* p, const this_& src) noexcept(RequireNoexceptCopy);

      template(typename T)  //
          (requires(
              !RequireNoexceptCopy ||
              std::is_nothrow_copy_constructible_v<T>))  //
          void
          operator()(void* p, const T& src) const
          noexcept(std::is_nothrow_copy_constructible_v<T>) {
        ::new (p) T(src);
      }
    };

  }  // namespace detail
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
