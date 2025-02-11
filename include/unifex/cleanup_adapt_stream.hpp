/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
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
namespace _cleanup_adapt {
template <typename Stream, typename AdaptFunc>
struct _stream final {
  struct type;
};
template <typename Stream, typename AdaptFunc>
using stream =
    typename _stream<remove_cvref_t<Stream>, std::decay_t<AdaptFunc>>::type;

template <typename Stream, typename AdaptFunc>
struct _stream<Stream, AdaptFunc>::type final {
  UNIFEX_NO_UNIQUE_ADDRESS Stream innerStream_;
  UNIFEX_NO_UNIQUE_ADDRESS AdaptFunc adapter_;

  friend auto
  tag_invoke(tag_t<next>, type& s) noexcept(noexcept(next(s.innerStream_)))
      -> next_sender_t<Stream> {
    return next(s.innerStream_);
  }

  friend auto tag_invoke(tag_t<cleanup>, type& s) noexcept(
      noexcept(std::invoke(s.adapter_, cleanup(s.innerStream_))))
      -> std::invoke_result_t<AdaptFunc&, cleanup_sender_t<Stream>> {
    return std::invoke(s.adapter_, cleanup(s.innerStream_));
  }
};
}  // namespace _cleanup_adapt

namespace _cleanup_adapt_cpo {
inline constexpr struct _fn final {
  template <typename Stream, typename AdaptFunc>
  constexpr auto operator()(Stream&& stream, AdaptFunc&& adapt) const noexcept(
      std::is_nothrow_constructible_v<remove_cvref_t<Stream>, Stream>&&
          std::is_nothrow_constructible_v<std::decay_t<AdaptFunc>, AdaptFunc>) {
    return _cleanup_adapt::stream<Stream, AdaptFunc>{
        (Stream&&)stream, (AdaptFunc&&)adapt};
  }
} cleanup_adapt_stream{};
}  // namespace _cleanup_adapt_cpo

using _cleanup_adapt_cpo::cleanup_adapt_stream;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
