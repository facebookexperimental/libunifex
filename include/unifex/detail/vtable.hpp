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

#include <unifex/config.hpp>
#include <unifex/overload.hpp>
#include <unifex/this.hpp>
#include <unifex/type_traits.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  namespace detail
  {
    // Queries about whether or not a given type, T, supports a given CPO.
    template <typename T, typename CPO, typename Sig>
    inline constexpr bool supports_type_erased_cpo_v = false;

    template <typename T, typename CPO, typename Ret, typename... Args>
    inline constexpr bool supports_type_erased_cpo_v<T, CPO, Ret(Args...)> =
        is_callable_r_v<Ret, CPO, replace_this_t<Args, T>...>;

    template <typename T, typename CPO, typename Ret, typename... Args>
    inline constexpr bool
        supports_type_erased_cpo_v<T, CPO, Ret(Args...) noexcept> =
            is_nothrow_callable_r_v<Ret, CPO, replace_this_t<Args, T>...>;

    template <typename T, typename... CPOs>
    inline constexpr bool supports_type_erased_cpos_v =
        (supports_type_erased_cpo_v<
            T,
            CPOs,
            typename CPOs::type_erased_signature_t> && ...);

    template <
        typename CPO,
        typename T,
        bool NoExcept,
        typename Ret,
        typename... Args>
    Ret _vtable_invoke(
        CPO cpo,
        replace_this_with_void_ptr_t<Args>... args) noexcept(NoExcept) {
      static_assert(!NoExcept || noexcept(extract_this<Args...>{}(args...)));

      void* thisPointer = extract_this<Args...>{}(args...);

      static_assert(
          !NoExcept ||
          noexcept(Ret(((CPO &&) cpo)(replace_this<Args>::get(
              (decltype(args)&&)args, *static_cast<T*>(thisPointer))...))));

      return ((CPO &&) cpo)(replace_this<Args>::get(
          (decltype(args)&&)args, *static_cast<T*>(thisPointer))...);
    }

    template <typename... CPOs>
    struct inline_vtable_holder;

    template <
        typename CPO,
        typename Sig = typename CPO::type_erased_signature_t>
    struct vtable_entry;

    template <typename CPO, typename Ret, typename... Args>
    struct vtable_entry<CPO, Ret(Args...) noexcept> {
      using fn_t =
          Ret(base_cpo_t<CPO>, replace_this_with_void_ptr_t<Args>...) noexcept;

      constexpr fn_t* get() const noexcept { return fn_; }

      template <typename T>
      static constexpr vtable_entry create() noexcept {
        return vtable_entry{
            &_vtable_invoke<base_cpo_t<CPO>, T, true, Ret, Args...>};
      }

    private:
      template <typename... CPOs>
      friend struct inline_vtable_holder;

      explicit constexpr vtable_entry(fn_t* fn) noexcept : fn_(fn) {}

      fn_t* fn_;
    };

    template <typename CPO, typename Ret, typename... Args>
    struct vtable_entry<CPO, Ret(Args...)> {
      using fn_t = Ret(base_cpo_t<CPO>, replace_this_with_void_ptr_t<Args>...);

      constexpr fn_t* get() const noexcept { return fn_; }

      template <typename T>
      static constexpr vtable_entry create() noexcept {
        return vtable_entry{
            &_vtable_invoke<base_cpo_t<CPO>, T, false, Ret, Args...>};
      }

    private:
      template <typename... CPOs>
      friend struct inline_vtable_holder;

      explicit constexpr vtable_entry(fn_t* fn) noexcept : fn_(fn) {}

      fn_t* fn_;
    };

    template <typename... CPOs>
    struct vtable : private vtable_entry<CPOs>... {
      template <typename T>
      static constexpr vtable create() noexcept {
        return vtable{vtable_entry<CPOs>::template create<T>()...};
      }

      template <typename CPO>
      constexpr auto get() const noexcept -> typename vtable_entry<CPO>::fn_t* {
        const vtable_entry<CPO>& entry = *this;
        return entry.get();
      }

    private:
      friend inline_vtable_holder<CPOs...>;

      explicit constexpr vtable(vtable_entry<CPOs>... entries) noexcept
        : vtable_entry<CPOs>{entries}... {}
    };

    template <typename... CPOs>
    struct indirect_vtable_holder {
      template <typename T>
      static indirect_vtable_holder create() {
        static constexpr vtable<CPOs...> v =
            vtable<CPOs...>::template create<T>();
        return indirect_vtable_holder{v};
      }

      const vtable<CPOs...>& operator*() const noexcept { return *vtable_; }

      const vtable<CPOs...>* operator->() const noexcept { return vtable_; }

    private:
      constexpr indirect_vtable_holder(const vtable<CPOs...>& vtable)
        : vtable_(&vtable) {}

      const vtable<CPOs...>* vtable_;
    };

    template <typename... CPOs>
    struct inline_vtable_holder {
      constexpr inline_vtable_holder(
          const inline_vtable_holder& other) noexcept = default;

      // Casting from an inline_vtable with a superset of vtable entries
      template <typename... OtherCPOs>
      /* implicit */ inline_vtable_holder(
          const inline_vtable_holder<OtherCPOs...>& other) noexcept
        : vtable_(vtable_entry<CPOs>(other->template get<CPOs>())...) {}

      // Casting from an indirect_vtable with a superset of vtable entries
      template <typename... OtherCPOs>
      /* implicit */ inline_vtable_holder(
          indirect_vtable_holder<OtherCPOs...> other) noexcept
        : vtable_(vtable_entry<CPOs>(other->template get<CPOs>())...) {}

      template <typename T>
      static constexpr inline_vtable_holder create() {
        return inline_vtable_holder{vtable<CPOs...>::template create<T>()};
      }

      const vtable<CPOs...>& operator*() const noexcept { return vtable_; }

      const vtable<CPOs...>* operator->() const noexcept { return &vtable_; }

    private:
      constexpr inline_vtable_holder(const vtable<CPOs...>& vtable)
        : vtable_(vtable) {}

      vtable<CPOs...> vtable_;
    };

  }  // namespace detail
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
