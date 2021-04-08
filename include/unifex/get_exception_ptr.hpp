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

#include <unifex/exception.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>

#include <memory>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  namespace _get_exception_ptr
  {
    inline const struct _fn {
      // forward std::exception_ptr
      std::exception_ptr operator()(std::exception_ptr &&eptr) const noexcept {
        return std::forward<std::exception_ptr>(eptr);
      }

      // convert std::exception based types to std::exception_ptr
      template(typename Exception)(
          requires std::is_base_of_v<std::exception, Exception>)
          std::exception_ptr
          operator()(Exception &&ex) const noexcept {
        return make_exception_ptr(std::forward<Exception>(ex));
      }

      // use customization point
      // to resolve ErrorCode -> std::exception_ptr conversion
      template(typename ErrorCode)
          (requires is_tag_invocable_v<_fn, ErrorCode>)
      std::exception_ptr operator()(ErrorCode &&error) const noexcept {
        return tag_invoke(*this, std::forward<ErrorCode>(error));
      }
    } get_exception_ptr{};

    // default std::error_code -> std::exception_ptr conversion
    std::exception_ptr tag_invoke(tag_t<get_exception_ptr>, std::error_code &&error) noexcept {
      return make_exception_ptr(
          std::system_error{std::forward<std::error_code>(error)});
    }

  }  // namespace _get_exception_ptr_cpo

  using _get_exception_ptr::get_exception_ptr;
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
