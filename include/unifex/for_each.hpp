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

#include <unifex/config.hpp>
#include <unifex/reduce_stream.hpp>
#include <unifex/transform.hpp>
#include <unifex/type_traits.hpp>

namespace unifex {
namespace _for_each {
  UNIFEX_INLINE_VAR constexpr struct _fn {
    private:
      template<bool>
      struct _impl {
        template <typename Stream, typename Func>
        auto operator()(Stream&& stream, Func&& func) const
            noexcept(is_nothrow_tag_invocable_v<_fn, Stream, Func>)
            -> tag_invoke_result_t<_fn, Stream, Func> {
          return unifex::tag_invoke(_fn{}, (Stream&&) stream, (Func&&) func);
        }
      };
    public:
      template <typename Stream, typename Func>
      auto operator()(Stream&& stream, Func&& func) const
        noexcept(is_nothrow_callable_v<
            _impl<is_tag_invocable_v<_fn, Stream, Func>>, Stream, Func>)
        -> callable_result_t<
            _impl<is_tag_invocable_v<_fn, Stream, Func>>, Stream, Func> {
      return _impl<is_tag_invocable_v<_fn, Stream, Func>>{}(
        (Stream&&) stream, (Func&&) func);
    }
  } for_each{};

  template<>
  struct _fn::_impl<false> {
    template <typename Stream, typename Func>
    auto operator()(Stream&& stream, Func&& func) const {
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
  };
} // namespace _for_each

using _for_each::for_each;
} // namespace unifex
