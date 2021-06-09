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
#include <unifex/detail/unifex_fwd.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/detail/concept_macros.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
  namespace _tag_invoke {
    void tag_invoke();

    struct _fn {
      template <typename CPO, typename... Args>
      constexpr auto operator()(CPO cpo, Args&&... args) const
          noexcept(noexcept(tag_invoke((CPO &&) cpo, (Args &&) args...)))
          -> decltype(tag_invoke((CPO &&) cpo, (Args &&) args...)) {
        return tag_invoke((CPO &&) cpo, (Args &&) args...);
      }
    };

    template <typename CPO, typename... Args>
    using tag_invoke_result_t = decltype(
        tag_invoke(UNIFEX_DECLVAL(CPO &&), UNIFEX_DECLVAL(Args &&)...));

    using yes_type = char;
    using no_type = char(&)[2];

    template <typename CPO,
              typename... Args,
              typename = tag_invoke_result_t<CPO, Args...>>
    yes_type try_tag_invoke(int) //
        noexcept(noexcept(tag_invoke(
            UNIFEX_DECLVAL(CPO &&), UNIFEX_DECLVAL(Args &&)...)));

    template <typename CPO, typename... Args>
    no_type try_tag_invoke(...) noexcept(false);

    template <template <typename...> class T, typename... Args>
    struct defer {
      using type = T<Args...>;
    };

    namespace _cpo {
      inline constexpr _fn tag_invoke{};
    }
  } // namespace _tag_invoke
  using namespace _tag_invoke::_cpo;

  template <auto& CPO>
  using tag_t = remove_cvref_t<decltype(CPO)>;

  // Manually implement the traits here rather than defining them in terms of
  // the corresponding std::invoke_result/is_invocable/is_nothrow_invocable
  // traits to improve compile-times. We don't need all of the generality of the
  // std:: traits and the tag_invoke traits are used heavily through libunifex
  // so optimising them for compile time makes a big difference.

  using _tag_invoke::tag_invoke_result_t;

  template <typename CPO, typename... Args>
  inline constexpr bool is_tag_invocable_v =
      (sizeof(_tag_invoke::try_tag_invoke<CPO, Args...>(0)) ==
       sizeof(_tag_invoke::yes_type));

  template <typename CPO, typename... Args>
  struct tag_invoke_result
    : conditional_t<
          is_tag_invocable_v<CPO, Args...>,
          _tag_invoke::defer<tag_invoke_result_t, CPO, Args...>,
          detail::_empty<>>
  {};

  template <typename CPO, typename... Args>
  using is_tag_invocable = std::bool_constant<is_tag_invocable_v<CPO, Args...>>;

  template <typename CPO, typename... Args>
  inline constexpr bool is_nothrow_tag_invocable_v =
      noexcept(_tag_invoke::try_tag_invoke<CPO, Args...>(0));

  template <typename CPO, typename... Args>
  using is_nothrow_tag_invocable =
      std::bool_constant<is_nothrow_tag_invocable_v<CPO, Args...>>;

  template <typename CPO, typename... Args>
  UNIFEX_CONCEPT tag_invocable =
      (sizeof(_tag_invoke::try_tag_invoke<CPO, Args...>(0)) ==
       sizeof(_tag_invoke::yes_type));

  template <typename Fn>
  using meta_tag_invoke_result =
      meta_quote1_<tag_invoke_result_t>::bind_front<Fn>;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
