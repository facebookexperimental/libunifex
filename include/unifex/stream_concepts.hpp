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

#include <unifex/tag_invoke.hpp>
#include <unifex/sender_concepts.hpp>

namespace unifex {
namespace _streams {
  inline constexpr struct _next_fn {
  private:
    template<bool>
    struct _impl {
      template <typename Stream>
      auto operator()(Stream& stream) const
          noexcept(is_nothrow_tag_invocable_v<_next_fn, Stream&>)
          -> tag_invoke_result_t<_next_fn, Stream&> {
        return unifex::tag_invoke(_next_fn{}, stream);
      }
    };
  public:
    template <typename Stream>
    auto operator()(Stream& stream) const
        noexcept(is_nothrow_callable_v<
            _impl<is_tag_invocable_v<_next_fn, Stream&>>, Stream&>)
        -> callable_result_t<
            _impl<is_tag_invocable_v<_next_fn, Stream&>>, Stream&> {
      return _impl<is_tag_invocable_v<_next_fn, Stream&>>{}(stream);
    }
  } next{};

  template<>
  struct _next_fn::_impl<false> {
    template <typename Stream>
    auto operator()(Stream& stream) const
        noexcept(noexcept(stream.next()))
        -> decltype(stream.next()) {
      return stream.next();
    }
  };

  inline constexpr struct _cleanup_fn {
  private:
    template<bool>
    struct _impl {
      template <typename Stream>
      auto operator()(Stream& stream) const
          noexcept(is_nothrow_tag_invocable_v<_cleanup_fn, Stream&>)
          -> tag_invoke_result_t<_cleanup_fn, Stream&> {
        return unifex::tag_invoke(_cleanup_fn{}, stream);
      }
    };
  public:
    template <typename Stream>
    auto operator()(Stream& stream) const
        noexcept(is_nothrow_callable_v<
            _impl<is_tag_invocable_v<_cleanup_fn, Stream&>>, Stream&>)
        -> callable_result_t<
            _impl<is_tag_invocable_v<_cleanup_fn, Stream&>>, Stream&> {
      return _impl<is_tag_invocable_v<_cleanup_fn, Stream&>>{}(stream);
    }
  } cleanup{};

  template<>
  struct _cleanup_fn::_impl<false> {
    template <typename Stream>
    auto operator()(Stream& stream) const
        noexcept(noexcept(stream.cleanup()))
        -> decltype(stream.cleanup()) {
      return stream.cleanup();
    }
  };
} // namespace _streams

using _streams::next;
using _streams::cleanup;

template <typename Stream>
using next_sender_t = decltype(next(std::declval<Stream&>()));

template <typename Stream>
using cleanup_sender_t = decltype(cleanup(std::declval<Stream&>()));

template <typename Stream, typename Receiver>
using next_operation_t = connect_result_t<next_sender_t<Stream>, Receiver>;

template <typename Stream, typename Receiver>
using cleanup_operation_t = connect_result_t<cleanup_sender_t<Stream>, Receiver>;

} // namespace unifex
