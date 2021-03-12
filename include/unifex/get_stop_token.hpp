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

#if !UNIFEX_NO_COROUTINES
#include <unifex/await_transform.hpp>
#endif

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _get_stop_token {
  inline const struct _fn {
#if !UNIFEX_NO_COROUTINES
  private:
    class _awaitable {
      template <typename StopToken>
      struct _awaiter {
        StopToken stoken_;
        bool await_ready() const noexcept {
          return true;
        }
        void await_suspend(coro::coroutine_handle<>) const noexcept {
        }
        StopToken await_resume() noexcept {
          return (StopToken&&) stoken_;
        }
      };
      template <typename StopToken>
      _awaiter(StopToken) -> _awaiter<StopToken>;

      template <typename Promise>
      friend auto tag_invoke(tag_t<await_transform>, Promise& promise, _awaitable) noexcept {
        return _awaiter{_fn{}(promise)};
      }
    };

  public:
    // `co_await get_stop_token()` to fetch a coroutine's current stop token.
    [[nodiscard]] constexpr _awaitable operator()() const noexcept {
      return {};
    }
#endif

    template (typename T)
      (requires (!tag_invocable<_fn, const T&>))
    constexpr auto operator()(const T&) const noexcept
        -> unstoppable_token {
      return unstoppable_token{};
    }

    template (typename T)
      (requires tag_invocable<_fn, const T&>)
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
