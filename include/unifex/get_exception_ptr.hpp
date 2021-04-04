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

#include <concepts>
#include <memory>
#include <system_error>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  inline const struct _get_exception_ptr_cpo {
    template <typename ErrorCode>
    constexpr auto operator()(ErrorCode&& error) const noexcept
        -> tag_invoke_result_t<_get_exception_ptr_cpo, ErrorCode> {
      return unifex::tag_invoke(*this, (ErrorCode &&) error);
    }
  } get_exception_ptr{};

  template <typename Error>
  concept exception_ptr_convertible = requires(Error error) {
    { get_exception_ptr(std::move(error)) } -> std::same_as<std::exception_ptr>;
  };

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
