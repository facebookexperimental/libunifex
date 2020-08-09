/*
 * Copyright 2019-present Facebook, Inc.
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

namespace unifex {
  namespace detail {
    template <typename, typename = void>
    extern const bool _is_executor;

    template <typename E, typename R, typename = void>
    extern const bool _can_execute;

    template <typename S, typename F, typename = void>
    extern const bool _can_submit;
  } // detail

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

  struct enable_operator_composition;
} // namespace unifex
