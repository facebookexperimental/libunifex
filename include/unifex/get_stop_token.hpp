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

#include <unifex/stop_token_concepts.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/unstoppable_token.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _get_stop_token {
  inline const struct _fn {
  private:
#if !UNIFEX_NO_COROUTINES
    struct awaiter_ {
      // This will fail for coroutine promises storing other kinds of
      // stop tokens, but we don't currently have examples of that right
      // now.
      inplace_stop_token stoken_;
      bool await_ready() const noexcept {
        return false;
      }
      template <typename Promise>
      bool await_suspend(coro::coroutine_handle<Promise> coro) noexcept {
        stoken_ = _fn{}(coro.promise());
        return false; // don't suspend
      }
      inplace_stop_token await_resume() const noexcept {
        return stoken_;
      }
    };
    friend awaiter_ operator co_await(_fn) noexcept {
      return {};
    }
#endif
  public:
    template <typename T>
    constexpr auto operator()(const T&) const noexcept
        -> std::enable_if_t<!is_tag_invocable_v<_fn, const T&>,
                            unstoppable_token> {
      return unstoppable_token{};
    }

    template <typename T>
    constexpr auto operator()(const T& object) const noexcept
        -> tag_invoke_result_t<_fn, const T&> {
      static_assert(
          is_nothrow_tag_invocable_v<_fn, const T&>,
          "get_stop_token() customisations must be declared noexcept");
      return tag_invoke(_fn{}, object);
    }
  } get_stop_token{};
} // namespace _get_stop_token

using _get_stop_token::get_stop_token;

template <typename T>
using get_stop_token_result_t =
    callable_result_t<decltype(get_stop_token), T>;

template <typename Receiver>
using stop_token_type_t =
    remove_cvref_t<get_stop_token_result_t<Receiver>>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
