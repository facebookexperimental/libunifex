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

#include <cstdint>

namespace unifex::win32
{
  using handle_t = void*;              // HANDLE
  using ulong_ptr_t = std::uintptr_t;  // ULONG_PTR
  using long_ptr_t = std::intptr_t;    // LONG_PTR
  using dword_t = unsigned long;       // DWORD
  using socket_t = std::uintptr_t;     // SOCKET
  using ulong_t = unsigned long;       // ULONG
  using long_t = long;                 // LONG

#if defined(_MSC_VER)
#  if defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wgnu-anonymous-struct"
#    pragma GCC diagnostic ignored "-Wnested-anon-types"
#  else
#    pragma warning(push)
#    pragma warning(disable : 4201)  // non-standard anonymous struct/union
#  endif
#endif
#if defined(__GNUC__)
#  define UNIFEX_NAMELESS_UNION __extension__
#else
#  define UNIFEX_NAMELESS_UNION
#endif
  struct overlapped {
    ulong_ptr_t Internal;
    ulong_ptr_t InternalHigh;
    UNIFEX_NAMELESS_UNION union {
      struct {
        dword_t Offset;
        dword_t OffsetHigh;
      };
      void* Pointer;
    };
    handle_t hEvent;
  };
#undef UNIFEX_NAMELESS_UNION
#if defined(_MSC_VER)
#  if defined(__clang__)
#    pragma GCC diagnostic pop
#  else
#    pragma warning(pop)
#  endif
#endif

  struct wsabuf {
    constexpr wsabuf() noexcept : len(0), buf(nullptr) {}

    wsabuf(void* p, ulong_t sz) noexcept
      : len(sz)
      , buf(reinterpret_cast<char*>(p)) {}

    ulong_t len;
    char* buf;
  };

}  // namespace unifex::win32
