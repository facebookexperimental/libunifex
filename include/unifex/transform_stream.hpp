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
#include <unifex/bind_back.hpp>

#include <functional>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _tfx_stream {
  struct _fn {
    template <typename StreamSender, typename Func>
    auto operator()(StreamSender&& stream, Func&& func) const {
      return next_adapt_stream(
          (StreamSender &&) stream, [func = (Func &&) func](auto&& sender) mutable {
            return transform((decltype(sender))sender, std::ref(func));
          });
    }
    template <typename Func>
    constexpr auto operator()(Func&& func) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, Func>)
        -> bind_back_result_t<_fn, Func> {
      return bind_back(*this, (Func&&)func);
    }
  };
} // namespace _tfx_stream

inline constexpr _tfx_stream::_fn transform_stream {};
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
