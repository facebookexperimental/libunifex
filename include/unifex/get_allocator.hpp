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

#include <unifex/tag_invoke.hpp>
#include <unifex/cstddef.hpp>

#include <cstddef>
#include <memory>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _get_alloc {
  inline const struct _fn {
    template <typename T>
    constexpr auto operator()(const T&) const noexcept
        -> std::enable_if_t<!is_tag_invocable_v<_fn, const T&>,
                            std::allocator<unifex::byte>> {
      return std::allocator<unifex::byte>{};
    }

    template <typename T>
    constexpr auto operator()(const T& object) const noexcept
        -> tag_invoke_result_t<_fn, const T&> {
      return tag_invoke(*this, object);
    }
  } get_allocator{};
} // namespace _get_alloc

using _get_alloc::get_allocator;

template <typename T>
using get_allocator_t = decltype(get_allocator(std::declval<T>()));

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
