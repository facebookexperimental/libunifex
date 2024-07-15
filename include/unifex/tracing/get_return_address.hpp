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

#include <unifex/sender_concepts.hpp>
#include <unifex/tracing/async_stack.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _get_return_address {

template <typename T>
instruction_ptr default_return_address() noexcept;

struct _fn {
  template(typename T)                           //
      (requires(!tag_invocable<_fn, const T&>))  //
      instruction_ptr
      operator()(const T&) const noexcept {
    // this address is mostly bonkers *but* it points at a function with
    // the sender's type in it, which is (hopefully) better than nothing
    return default_return_address<T>();
  }

  template(typename T)                         //
      (requires tag_invocable<_fn, const T&>)  //
      constexpr instruction_ptr
      operator()(const T& sender) const noexcept {
    static_assert(
        is_nothrow_tag_invocable_v<_fn, const T&>,
        "get_return_address() customisations must be declared noexcept");
    static_assert(same_as<instruction_ptr, tag_invoke_result_t<_fn, const T&>>);
    return tag_invoke(_fn{}, sender);
  }

  UNIFEX_NO_INLINE static instruction_ptr get_return_address() noexcept {
    return instruction_ptr::read_return_address();
  }
};

template <typename T>
UNIFEX_NO_INLINE instruction_ptr default_return_address() noexcept {
  return _fn::get_return_address();
}

}  // namespace _get_return_address

inline constexpr _get_return_address::_fn get_return_address{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
