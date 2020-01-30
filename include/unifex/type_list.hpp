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

#include <unifex/type_traits.hpp>

#include <type_traits>

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
              std::conditional_t<
                is_one_of_v<Us, Ts...>,
                type_list<>,
                type_list<Us>>...>::type,
          OtherLists...> {};

} // namespace unifex
