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

#include <unifex/get_allocator.hpp>
#include <unifex/with_query_value.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _with_alloc_cpo {
  struct _fn {
    template <typename Sender, typename Allocator>
    auto operator()(Sender &&sender, Allocator &&allocator) const {
      return with_query_value((Sender &&) sender, get_allocator,
                              (Allocator &&) allocator);
    }
    template <typename Allocator>
    constexpr auto operator()(Allocator&& allocator) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, Allocator>)
        -> bind_back_result_t<_fn, Allocator> {
      return bind_back(*this, (Allocator&&)allocator);
    }
  };
} // namespace _with_alloc_cpo

inline constexpr _with_alloc_cpo::_fn with_allocator {};
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
