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

inline constexpr struct next_cpo {
  template <typename Stream>
  friend auto tag_invoke(next_cpo, Stream& s) noexcept(noexcept(s.next()))
      -> decltype(s.next()) {
    return s.next();
  }

  template <typename Stream>
  constexpr auto operator()(Stream& stream) const
      noexcept(is_nothrow_tag_invocable_v<next_cpo, Stream&>)
          -> tag_invoke_result_t<next_cpo, Stream&> {
    return tag_invoke(*this, stream);
  }
} next{};

inline constexpr struct cleanup_cpo {
  template <typename Stream>
  friend auto tag_invoke(cleanup_cpo, Stream& s) noexcept(noexcept(s.cleanup()))
      -> decltype(s.cleanup()) {
    return s.cleanup();
  }

  template <typename Stream>
  constexpr auto operator()(Stream& stream) const
      noexcept(is_nothrow_tag_invocable_v<cleanup_cpo, Stream&>)
          -> tag_invoke_result_t<cleanup_cpo, Stream&> {
    return tag_invoke(*this, stream);
  }
} cleanup{};

template <typename Stream>
using next_sender_t = decltype(next(std::declval<Stream&>()));

template <typename Stream>
using cleanup_sender_t = decltype(cleanup(std::declval<Stream&>()));

template <typename Stream, typename Receiver>
using next_operation_t = operation_t<next_sender_t<Stream>, Receiver>;

template <typename Stream, typename Receiver>
using cleanup_operation_t = operation_t<cleanup_sender_t<Stream>, Receiver>;

} // namespace unifex
