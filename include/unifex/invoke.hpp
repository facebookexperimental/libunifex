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
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>

#include <functional>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _co_invoke {
inline constexpr struct _fn {
    template (typename Fn, typename... Args)
      (requires tag_invocable<
          _fn,
          type_identity<std::invoke_result_t<Fn, Args...>>,
          Fn,
          Args...>)
    UNIFEX_ALWAYS_INLINE constexpr auto operator()(Fn&& fn, Args&&... args) const
      noexcept(is_nothrow_tag_invocable_v<
          _fn,
          type_identity<std::invoke_result_t<Fn, Args...>>,
          Fn,
          Args...>)
      -> tag_invoke_result_t<
          _fn,
          type_identity<std::invoke_result_t<Fn, Args...>>,
          Fn,
          Args...> {
      return tag_invoke(
          *this,
          type_identity<std::invoke_result_t<Fn, Args...>>{},
          (Fn&&) fn,
          (Args&&) args...);
    }
} co_invoke{};
} // namespace _co_invoke

using _co_invoke::co_invoke;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
