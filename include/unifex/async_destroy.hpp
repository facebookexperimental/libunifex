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

#include <unifex/just.hpp>
#include <unifex/tag_invoke.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _async_destroy {

inline constexpr struct _destroy_fn {
private:
  template <typename Resource>
  static auto try_invoke_member(Resource& resource, int) noexcept(
      noexcept(resource.destroy())) -> decltype(resource.destroy()) {
    return resource.destroy();
  }

  template <typename Resource>
  [[deprecated(
      "Can't async_destroy an object with no async_destroy customization; add "
      "a no-op if that's what's intended.")]]  //
  static auto
  try_invoke_member(Resource&, double) noexcept {
    return just();
  }

public:
  template(typename Resource)                           //
      (requires tag_invocable<_destroy_fn, Resource&>)  //
      constexpr auto
      operator()(Resource& resource) const
      noexcept(is_nothrow_tag_invocable_v<_destroy_fn, Resource&>) {
    return tag_invoke(_destroy_fn{}, resource);
  }

  template(typename Resource)                             //
      (requires(!tag_invocable<_destroy_fn, Resource&>))  //
      constexpr auto
      operator()(Resource& resource) const
      noexcept(noexcept(try_invoke_member(resource, 1))) {
    return try_invoke_member(resource, 1);
  }
} async_destroy{};
}  // namespace _async_destroy
using _async_destroy::async_destroy;
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
