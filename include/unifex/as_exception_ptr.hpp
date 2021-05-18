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
#include <system_error>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  namespace _as_exception_ptr
  {
    inline constexpr struct _fn {
      // forward std::exception_ptr
      std::exception_ptr operator()(std::exception_ptr eptr) const noexcept {
        return eptr;
      }

      // default std::error_code to std::exception_ptr conversion
      std::exception_ptr operator()(std::error_code&& error) const noexcept {
        return make_exception_ptr(
            std::system_error{(std::error_code &&) error});
      }

      // convert std::exception based types to std::exception_ptr
      template(typename Exception)
          (requires (!tag_invocable<_fn, Exception>) AND
            derived_from<Exception, std::exception>)
      std::exception_ptr operator()(Exception&& ex) const noexcept {
        return make_exception_ptr((Exception &&) ex);
      }

      // use customization point
      // to resolve ErrorCode -> std::exception_ptr conversion
      template(typename ErrorCode)
          (requires tag_invocable<_fn, ErrorCode>)
      std::exception_ptr operator()(ErrorCode&& error) const noexcept {
        static_assert(nothrow_tag_invocable<_fn, ErrorCode>, 
          "as_exception_ptr() customisations must be declared noexcept");
        return tag_invoke(*this, (ErrorCode &&) error);
      }
    } as_exception_ptr{};
  }  // namespace _as_exception_ptr

  using _as_exception_ptr::as_exception_ptr;
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
