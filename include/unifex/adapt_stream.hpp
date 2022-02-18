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

#include <unifex/stream_concepts.hpp>

#include <functional>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _adapt_stream {
template <typename Stream, typename NextAdaptFunc, typename CleanupAdaptFunc>
struct _adapted {
  struct type;
};
template <typename Stream, typename NextAdaptFunc, typename CleanupAdaptFunc = void>
using adapted = typename _adapted<
    remove_cvref_t<Stream>,
    std::decay_t<NextAdaptFunc>,
    std::decay_t<CleanupAdaptFunc>>::type;

template <typename Stream, typename NextAdaptFunc, typename CleanupAdaptFunc>
struct _adapted<Stream, NextAdaptFunc, CleanupAdaptFunc>::type {
  Stream innerStream_;
  NextAdaptFunc nextAdapter_;
  CleanupAdaptFunc cleanupAdapter_;

  friend auto tag_invoke(tag_t<next>, type& s)
      -> std::invoke_result_t<NextAdaptFunc&, next_sender_t<Stream>> {
    return std::invoke(s.nextAdapter_, next(s.innerStream_));
  }

  friend auto tag_invoke(tag_t<cleanup>, type& s)
      -> std::invoke_result_t<CleanupAdaptFunc&, cleanup_sender_t<Stream>> {
    return std::invoke(s.cleanupAdapter_, cleanup(s.innerStream_));
  }
};

template <typename Stream, typename AdaptFunc>
struct _adapted<Stream, AdaptFunc, void> {
  struct type;
};
template <typename Stream, typename AdaptFunc>
struct _adapted<Stream, AdaptFunc, void>::type {
  Stream innerStream_;
  AdaptFunc adapter_;

  friend auto tag_invoke(tag_t<next>, type& s)
      -> std::invoke_result_t<AdaptFunc&, next_sender_t<Stream>> {
    return std::invoke(s.adapter_, next(s.innerStream_));
  }

  friend auto tag_invoke(tag_t<cleanup>, type& s)
      -> std::invoke_result_t<AdaptFunc&, cleanup_sender_t<Stream>> {
    return std::invoke(s.adapter_, cleanup(s.innerStream_));
  }
};
} // namespace _adapt_stream

namespace _adapt_stream_cpo {
  inline const struct _fn {
    template <typename Stream, typename AdapterFunc>
    auto operator()(Stream&& stream, AdapterFunc&& adapt) const
        -> _adapt_stream::adapted<Stream, AdapterFunc> {
      return {(Stream &&) stream, (AdapterFunc &&) adapt};
    }

    template <
        typename Stream,
        typename NextAdapterFunc,
        typename CleanupAdapterFunc>
    auto operator()(
        Stream&& stream,
        NextAdapterFunc&& adaptNext,
        CleanupAdapterFunc&& adaptCleanup) const
        -> _adapt_stream::adapted<Stream, NextAdapterFunc, CleanupAdapterFunc> {
      return {
          (Stream &&) stream,
          (NextAdapterFunc &&) adaptNext,
          (CleanupAdapterFunc &&) adaptCleanup};
    }
  } adapt_stream {};
} // namespace _adapt_stream_cpo
using _adapt_stream_cpo::adapt_stream;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
