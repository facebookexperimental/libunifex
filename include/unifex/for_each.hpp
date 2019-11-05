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

#include <unifex/reduce_stream.hpp>
#include <unifex/transform.hpp>
#include <unifex/type_traits.hpp>

namespace unifex {
namespace cpo {

inline constexpr struct for_each_cpo {
  template <typename Stream, typename Func>
  friend auto tag_invoke(for_each_cpo, Stream&& stream, Func&& func) {
    return transform(
        reduce_stream(
            (Stream &&) stream,
            unit{},
            [func = (Func &&) func](unit s, auto&&... values) mutable {
              std::invoke(func, (decltype(values))values...);
              return s;
            }),
        [](unit) noexcept {});
  }

  template <typename Stream, typename Func>
  auto operator()(Stream&& stream, Func&& func) const
      noexcept(noexcept(tag_invoke(*this, (Stream &&) stream, (Func &&) func)))
          -> decltype(tag_invoke(*this, (Stream &&) stream, (Func &&) func)) {
    return tag_invoke(*this, (Stream &&) stream, (Func &&) func);
  }
} for_each;

} // namespace cpo
} // namespace unifex
