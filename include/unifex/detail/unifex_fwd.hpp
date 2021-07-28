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

namespace unifex {
  namespace detail {
    template <typename, typename = void>
    extern const bool _is_executor;

    template <typename E, typename R, typename = void>
    extern const bool _can_execute;

    template <typename S, typename F, typename = void>
    extern const bool _can_submit;

    struct _ignore {
      template <typename T>
      /*implicit*/ _ignore(T&&) noexcept {}
    };
  } // namespace detail

  using detail::_ignore;

  namespace _execute_cpo {
    extern const struct _fn execute;
  } // namespace _execute_cpo
  using _execute_cpo::execute;

  namespace _submit_cpo {
    extern const struct _fn submit;
  } // namespace _submit_cpo
  using _submit_cpo::submit;

  namespace _connect_cpo {
    extern const struct _fn connect;
  } // namespace _connect_cpo
  using _connect_cpo::connect;

#if !UNIFEX_NO_COROUTINES
  namespace _await_tfx {
    extern const struct _fn await_transform;
  }
  using _await_tfx::await_transform;
#endif

} // namespace unifex
