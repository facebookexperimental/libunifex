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

#include <unifex/this.hpp>
#include <unifex/detail/vtable.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  template <typename Derived, typename CPO, bool NoExcept, typename Sig>
  struct _with_type_erased_tag_invoke;

  template <
      typename Derived,
      typename CPO,
      bool NoExcept,
      typename Ret,
      typename... Args>
  struct _with_type_erased_tag_invoke<Derived, CPO, NoExcept, Ret(Args...)> {
    struct type {
      friend Ret tag_invoke(
          base_cpo_t<CPO> cpo,
          replace_this_t<Args, Derived>... args) noexcept(NoExcept) {
        using cpo_t = base_cpo_t<CPO>;
        static_assert(
            !NoExcept ||
            noexcept(extract_this<Args...>{}((decltype(args)&&)args...)));
        auto&& t = extract_this<Args...>{}((decltype(args)&&)args...);
        void* objPtr = get_object_address(t);
        auto* fnPtr = get_vtable(t)->template get<CPO>();

        // Sanity check that all of the component expressions here are
        // noexcept so we don't end up with exception tables being generated
        // for this function.
        static_assert(!NoExcept || noexcept(get_object_address(t)));
        static_assert(
            !NoExcept || noexcept(get_vtable(t)->template get<CPO>()));
        static_assert(
            !NoExcept ||
            noexcept(fnPtr(
                (cpo_t &&) cpo,
                replace_this<Args>::get((decltype(args)&&)args, objPtr)...)));

        return fnPtr(
            (cpo_t &&) cpo,
            replace_this<Args>::get((decltype(args)&&)args, objPtr)...);
      }
    };
  };

  template <typename Derived, typename CPO, typename Ret, typename... Args>
  struct _with_type_erased_tag_invoke<
      Derived,
      CPO,
      false,
      Ret(Args...) noexcept>
    : _with_type_erased_tag_invoke<Derived, CPO, true, Ret(Args...)> {};

  // When defining a type-erasing wrapper type, Derived, you can privately inherit
  // from this class to have the type opt-in to customising the specified CPO.
  //
  // Each CPO must define its own nested CPO::type_erased_signature_t that specifies
  // which overload of that CPO is being type-erased. If a CPO doesn't define a
  // default type_erased_signature_t member, then you can use the `overload()`
  // helper to decorate a CPO with a specific signature.
  //
  // The Derived type must also define two hidden friends:
  //
  //  get_object_address(const Derived&) -> void*
  //    Returns pointer to type-erased object.
  //
  //  get_vtable(const Derived&) -> const vtable*
  //    Returns pointer to vtable containing function-pointers that operate on
  //    the type-erased object pointed to by the address returned from
  //    get_object_address().
  //
  // For example:
  //
  //   struct my_type_erasing_wrapper :
  //     private with_type_erased_tag_invoke<my_type_erasing_wrapper, tag_t<foo>>,
  //     private with_type_erased_tag_invoke<my_type_erasing_wrapper, overload_t<bar, void(const this_&, int)>> {
  //  
  //     using vtable_type = vtable<
  //       tag_t<foo>,
  //       overload_t<bar, void(const this_& int)>>
  //  
  //     ...
  //     friend void* get_object_address(const my_type_erasing_wrapper& x) noexcept {
  //       ...
  //     }
  //  
  //     friend const vtable_type* get_vtable(const my_type_erasing_wrapper& x) noexcept {
  //       ...
  //     }
  //   };
  template <typename Derived, typename CPO>
  using with_type_erased_tag_invoke = typename _with_type_erased_tag_invoke<
      Derived,
      CPO,
      false,
      typename CPO::type_erased_signature_t>::type;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
