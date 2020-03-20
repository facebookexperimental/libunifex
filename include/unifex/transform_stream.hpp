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

#include <unifex/next_adapt_stream.hpp>
#include <unifex/transform.hpp>

#include <functional>

namespace unifex {
namespace _tfx_stream {
  struct _fn {
    template <typename StreamSender, typename Func>
    auto operator()(StreamSender&& stream, Func&& func) const {
      return next_adapt_stream(
          (StreamSender &&) stream, [func = (Func &&) func](auto&& sender) mutable {
            return transform((decltype(sender))sender, (Func &&)func);
          });
    }
  };
} // namespace _tfx_stream

inline constexpr _tfx_stream::_fn transform_stream {};
} // namespace unifex
