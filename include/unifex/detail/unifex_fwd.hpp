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

    template <int=0>
    struct _empty {};
  } // namespace detail

  namespace _kv
  {
    template <typename Key, typename Value>
    struct _kv {
      struct type {
        using key_type = Key;
        using value_type = Value;
        UNIFEX_NO_UNIQUE_ADDRESS Key key;
        Value value;
      };
    };
  } // namespace _kv
  template <typename Key, typename Value>
  using kv = typename _kv::_kv<Key, Value>::type;

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
    struct _fn;
  } // namespace _await_tfx
  extern const _await_tfx::_fn await_transform;
#endif

  namespace _schedule {
    struct _fn;
  } // namespace _schedule
  extern const _schedule::_fn schedule;

  namespace _sf {
    template <const auto&, typename, typename>
    struct sender_for;
  } // namespace _sf
  using _sf::sender_for;

  namespace _xchg_cont {
    extern const struct _fn exchange_continuation;
  } // _xchg_cont
  using _xchg_cont::exchange_continuation;

  template <typename>
  struct sender_traits;

  template <const auto& CPO, typename Sender, typename Context>
  struct sender_traits<sender_for<CPO, Sender, Context>>;

} // namespace unifex
