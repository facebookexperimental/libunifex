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

#include <unifex/type_traits.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  // A template metaprogramming data-structure used to represent an
  // ordered list of types.
  template <typename... Ts>
  struct type_list {
    // Invoke the template metafunction with the type-list's elements
    // as arguments.
    template <template <typename...> class F>
    using apply = F<Ts...>;
  };

  // concat_type_lists<Lists...>
  //
  // Concatenates a variadic pack of type_list<Ts...> into a single
  // type_list that contains the concatenation of the elements of the
  // input type_lists.
  //
  // Result is produced via nested ::type.
  template <typename... Lists>
  struct concat_type_lists;

  template <>
  struct concat_type_lists<> {
    using type = type_list<>;
  };

  template <typename List>
  struct concat_type_lists<List> {
    using type = List;
  };

  template <typename... Ts, typename... Us>
  struct concat_type_lists<type_list<Ts...>, type_list<Us...>> {
      using type = type_list<Ts..., Us...>;
  };

  template <typename... Ts, typename... Us, typename... Vs, typename... OtherLists>
  struct concat_type_lists<type_list<Ts...>, type_list<Us...>, type_list<Vs...>, OtherLists...>
    : concat_type_lists<type_list<Ts..., Us..., Vs...>, OtherLists...> {};

  template <typename... UniqueLists>
  using concat_type_lists_t = typename concat_type_lists<UniqueLists...>::type;

  // concat_type_lists_unique<UniqueLists...>
  //
  // Result is produced via '::type' which will contain a type_list<Ts...> that
  // contains the unique elements from the input type_list types.
  // Assumes that the input lists already
  template <typename... UniqueLists>
  struct concat_type_lists_unique;

  template <>
  struct concat_type_lists_unique<> {
    using type = type_list<>;
  };

  template <typename UniqueList>
  struct concat_type_lists_unique<UniqueList> {
    using type = UniqueList;
  };

  template <typename... Ts, typename... Us, typename... OtherLists>
  struct concat_type_lists_unique<type_list<Ts...>, type_list<Us...>, OtherLists...>
    : concat_type_lists_unique<
          typename concat_type_lists<
              type_list<Ts...>,
              conditional_t<
                is_one_of_v<Us, Ts...>,
                type_list<>,
                type_list<Us>>...>::type,
          OtherLists...> {};

  template <typename... UniqueLists>
  using concat_type_lists_unique_t = typename concat_type_lists_unique<UniqueLists...>::type;

  namespace detail
  {
    template <
      template <typename...> class Outer,
      template <typename...> class Inner>
    struct type_list_nested_apply_impl {
      template <typename... Lists>
      using apply = Outer<typename Lists::template apply<Inner>...>;
    };
  }

  template <
    typename ListOfLists,
    template <typename...> class Outer,
    template <typename...> class Inner>
  using type_list_nested_apply_t = typename ListOfLists::template apply<
    detail::type_list_nested_apply_impl<Outer, Inner>::template apply>;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
