// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <unifex/stream_concepts.hpp>

#include <functional>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _complete_adapt {
template <typename Stream, typename AdaptFunc>
struct _stream {
  struct type;
};
template <typename Stream, typename AdaptFunc>
using stream =
    typename _stream<remove_cvref_t<Stream>, remove_cvref_t<AdaptFunc>>::type;

template <typename Stream, typename AdaptFunc>
struct _stream<Stream, AdaptFunc>::type {
  Stream innerStream_;
  AdaptFunc adapter_;

  friend auto tag_invoke(tag_t<next>, type& s) -> next_sender_t<Stream> {
    return next(s.innerStream_);
  }

  friend auto tag_invoke(tag_t<cleanup>, type& s)
      -> std::invoke_result_t<AdaptFunc&, cleanup_sender_t<Stream>> {
    return std::invoke(s.adapter_, cleanup(s.innerStream_));
  }
};
}  // namespace _complete_adapt

namespace _complete_adapt_cpo {
inline const struct _fn {
  template <typename Stream, typename AdaptFunc>
  auto operator()(Stream&& stream, AdaptFunc&& adapt) const {
    return _complete_adapt::stream<Stream, AdaptFunc>{
        (Stream &&) stream, (AdaptFunc &&) adapt};
  }
} complete_adapt_stream{};
}  // namespace _complete_adapt_cpo

using _complete_adapt_cpo::complete_adapt_stream;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
