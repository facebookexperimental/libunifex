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

#include <unifex/reduce_stream.hpp>
#include <unifex/then.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/bind_back.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _for_each {
  namespace _impl {
    template <typename Func>
    struct _map {
      Func func_;
      template(typename... Ts)
        (requires invocable<Func&, Ts...>)
      unit operator()(unit s, Ts&&... values)
          noexcept(std::is_nothrow_invocable_v<Func&, Ts...>) {
        std::invoke(func_, (Ts&&) values...);
        return s;
      }
    };
    struct _reduce {
      void operator()(unit) const noexcept {}
    };
  } // namespace _impl

  inline const struct _fn {
  private:
    template <typename Stream, typename Func>
    using _default_result_t =
        decltype(then(
          reduce_stream(
            UNIFEX_DECLVAL(Stream),
            unit{},
            UNIFEX_DECLVAL(_impl::_map<remove_cvref_t<Func>>)),
          _impl::_reduce{}));
    template <typename Stream, typename Func>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, Stream, Func>,
        meta_tag_invoke_result<_fn>,
        meta_quote2<_default_result_t>>::template apply<Stream, Func>;
  public:
    template(typename Stream, typename Func)
      (requires tag_invocable<_fn, Stream, Func>)
    auto operator()(Stream&& stream, Func&& func) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Stream, Func>)
        -> _result_t<Stream, Func> {
      return unifex::tag_invoke(_fn{}, (Stream&&) stream, (Func&&) func);
    }
    template(typename Stream, typename Func)
      (requires (!tag_invocable<_fn, Stream, Func>))
    auto operator()(Stream&& stream, Func&& func) const
        -> _result_t<Stream, Func> {
      return then(
          reduce_stream(
              (Stream &&) stream,
              unit{},
              _impl::_map<remove_cvref_t<Func>>{(Func &&) func}),
          _impl::_reduce{});
    }
    template <typename Func>
    constexpr auto operator()(Func&& f) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, Func>)
        -> bind_back_result_t<_fn, Func> {
      return bind_back(*this, (Func&&)f);
    }
  } for_each{};
} // namespace _for_each

using _for_each::for_each;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
