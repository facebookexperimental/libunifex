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

#include <unifex/config.hpp>
#include <unifex/bind_back.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _nest_cpo {

inline const struct _nest_fn final {
private:
  template <typename Scope, typename Sender>
  using nest_member_result_t =
      decltype(UNIFEX_DECLVAL(Scope&).nest(UNIFEX_DECLVAL(Sender)));

  template <typename Scope, typename Sender>
  static constexpr bool is_nest_member_nothrow_v =
      noexcept(UNIFEX_DECLVAL(Scope&).nest(UNIFEX_DECLVAL(Sender)));

  struct deref;

public:
  template(typename Sender, typename Scope)  //
      (requires typed_sender<Sender> AND tag_invocable<_nest_fn, Sender, Scope&>
           AND typed_sender<tag_invoke_result_t<_nest_fn, Sender, Scope&>>)  //
      auto
      operator()(Sender&& sender, Scope& scope) const
      noexcept(is_nothrow_tag_invocable_v<_nest_fn, Sender, Scope&>)
          -> tag_invoke_result_t<_nest_fn, Sender, Scope&> {
    return tag_invoke(_nest_fn{}, static_cast<Sender&&>(sender), scope);
  }

  template(typename Sender, typename Scope)  //
      (requires typed_sender<Sender> AND(
          !tag_invocable<_nest_fn, Sender, Scope&>)
           AND typed_sender<nest_member_result_t<Scope, Sender>>)  //
      auto
      operator()(Sender&& sender, Scope& scope) const
      noexcept(is_nest_member_nothrow_v<Scope, Sender>)
          -> nest_member_result_t<Scope, Sender> {
    return scope.nest(static_cast<Sender&&>(sender));
  }

  template <typename Scope>
  constexpr auto operator()(Scope& scope) const
      noexcept(is_nothrow_callable_v<tag_t<bind_back>, deref, Scope*>)
          -> bind_back_result_t<deref, Scope*>;
} nest{};

struct _nest_fn::deref final {
  template <typename Sender, typename Scope>
  constexpr auto operator()(Sender&& sender, Scope* scope) const
      noexcept(noexcept(_nest_fn{}(static_cast<Sender&&>(sender), *scope)))
          -> decltype(_nest_fn{}(static_cast<Sender&&>(sender), *scope)) {
    return _nest_fn{}(static_cast<Sender&&>(sender), *scope);
  }
};

template <typename Scope>
inline constexpr auto _nest_fn::operator()(Scope& scope) const
    noexcept(is_nothrow_callable_v<tag_t<bind_back>, deref, Scope*>)
        -> bind_back_result_t<deref, Scope*> {
  // bind_back will try to store a copy of any lvalue references it's passed,
  // which doesn't work for us here so we have to pass a scope pointer instead
  // of a scope reference.  we don't, in general, want to expose a
  // `nest(Sender&&, Scope*)` function so we use `_nest_fn::deref` to do the
  // indirection for us in this scenario.
  return bind_back(deref{}, &scope);
}

}  // namespace _nest_cpo

using _nest_cpo::nest;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
