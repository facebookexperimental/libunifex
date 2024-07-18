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

#include <unifex/tracing/async_stack.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _get_async_stack_frame {
struct _fn {
  template(typename T)                           //
      (requires(!tag_invocable<_fn, const T&>))  //
      constexpr AsyncStackFrame*
      operator()(const T&) const noexcept {
    return nullptr;
  }

  template(typename T)                         //
      (requires tag_invocable<_fn, const T&>)  //
      constexpr AsyncStackFrame*
      operator()(const T& sender) const noexcept {
    static_assert(
        is_nothrow_tag_invocable_v<_fn, const T&>,
        "get_async_stack_frame() customisations must be declared noexcept");
    static_assert(
        same_as<AsyncStackFrame*, tag_invoke_result_t<_fn, const T&>>);
    return tag_invoke(_fn{}, sender);
  }
};
}  // namespace _get_async_stack_frame

inline constexpr _get_async_stack_frame::_fn get_async_stack_frame{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
