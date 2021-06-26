/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <unifex/overload.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/this.hpp>

#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  namespace detail
  {
    inline namespace _gwo_cpo_ns
    {
      struct _get_wrapped_object_cpo {
        template(typename T)                                      //
            (requires tag_invocable<_get_wrapped_object_cpo, T>)  //
            auto
            operator()(T&& wrapper) const
            noexcept(is_nothrow_tag_invocable_v<_get_wrapped_object_cpo, T>)
                -> tag_invoke_result_t<_get_wrapped_object_cpo, T> {
          return unifex::tag_invoke(*this, static_cast<T&&>(wrapper));
        }
      };

      inline constexpr _get_wrapped_object_cpo get_wrapped_object{};
    }  // namespace _gwo_cpo_ns

    template <typename Derived, typename CPO, typename Sig>
    struct _with_forwarding_tag_invoke;

    template <typename Derived, typename CPO>
    using with_forwarding_tag_invoke = typename _with_forwarding_tag_invoke<
        Derived,
        base_cpo_t<CPO>,
        typename CPO::type_erased_signature_t>::type;

    // noexcept(false) specialisation
    template <typename Derived, typename CPO, typename Ret, typename... Args>
    struct _with_forwarding_tag_invoke<Derived, CPO, Ret(Args...)> {
      struct type {
        friend Ret tag_invoke(CPO cpo, replace_this_t<Args, Derived>... args) {
          auto& wrapper = extract_this<Args...>{}(args...);
          auto& wrapped = get_wrapped_object(wrapper);
          return static_cast<CPO&&>(cpo)(replace_this<Args>::get(
              static_cast<decltype(args)&&>(args), wrapped)...);
        }
      };
    };

    // noexcept(true) specialisation
    template <typename Derived, typename CPO, typename Ret, typename... Args>
    struct _with_forwarding_tag_invoke<Derived, CPO, Ret(Args...) noexcept> {
      struct type {
        friend Ret
        tag_invoke(CPO cpo, replace_this_t<Args, Derived>... args) noexcept {
          auto& wrapper = extract_this<Args...>{}(args...);
          auto& wrapped = get_wrapped_object(wrapper);

          // Sanity check that all of the component expressions here are
          // noexcept so we don't end up with exception tables being generated
          // for this function.
          static_assert(noexcept(extract_this<Args...>{}(args...)));
          static_assert(noexcept(get_wrapped_object(wrapper)));
          static_assert(
              noexcept(static_cast<CPO&&>(cpo)(replace_this<Args>::get(
                  static_cast<decltype(args)&&>(args), wrapped)...)));

          return static_cast<CPO&&>(cpo)(replace_this<Args>::get(
              static_cast<decltype(args)&&>(args), wrapped)...);
        }
      };
    };

  }  // namespace detail
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
