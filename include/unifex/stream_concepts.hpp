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

#include <unifex/tag_invoke.hpp>
#include <unifex/sender_concepts.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _streams {
  inline const struct _next_fn {
  private:
    template <typename Stream>
    using _member_next_result_t = decltype(UNIFEX_DECLVAL(Stream).next());
    template <typename Stream>
    using _result_t =
      typename conditional_t<
        tag_invocable<_next_fn, Stream>,
        meta_tag_invoke_result<_next_fn>,
        meta_quote1<_member_next_result_t>>::template apply<Stream>;
  public:
    template(typename Stream)
      (requires tag_invocable<_next_fn, Stream&>)
    auto operator()(Stream& stream) const
        noexcept(is_nothrow_tag_invocable_v<_next_fn, Stream&>)
        -> _result_t<Stream&> {
      return unifex::tag_invoke(_next_fn{}, stream);
    }
    template(typename Stream)
      (requires (!tag_invocable<_next_fn, Stream&>))
    auto operator()(Stream& stream) const
        noexcept(noexcept(stream.next()))
        -> _result_t<Stream&> {
      return stream.next();
    }
  } next{};

  inline const struct _cleanup_fn {
  private:
    template <typename Stream>
    using _member_cleanup_result_t = decltype(UNIFEX_DECLVAL(Stream).cleanup());
    template <typename Stream>
    using _result_t =
      typename conditional_t<
        tag_invocable<_cleanup_fn, Stream>,
        meta_tag_invoke_result<_cleanup_fn>,
        meta_quote1<_member_cleanup_result_t>>::template apply<Stream>;
  public:
    template(typename Stream)
      (requires tag_invocable<_cleanup_fn, Stream&>)
    auto operator()(Stream& stream) const
        noexcept(is_nothrow_tag_invocable_v<_cleanup_fn, Stream&>)
        -> _result_t<Stream&> {
      return unifex::tag_invoke(_cleanup_fn{}, stream);
    }
    template(typename Stream)
      (requires (!tag_invocable<_cleanup_fn, Stream&>))
    auto operator()(Stream& stream) const
        noexcept(noexcept(stream.cleanup()))
        -> _result_t<Stream&> {
      return stream.cleanup();
    }
  } cleanup{};
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

#include <unifex/detail/epilogue.hpp>
