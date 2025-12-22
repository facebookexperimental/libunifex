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

// __cpp_nontype_template_args value is broken in clang
// #if __cpp_nontype_template_args < 0x201911L

#if __cplusplus < 201911L
#  error \
      "Non-type template parameters with class types are not supported by this compiler"
#endif

#include <type_traits>

namespace unifex::_make_traits {

template <auto MemPtr>
struct define_trait;

template <typename Traits>
struct get_traits;

template <typename Traits, auto... MemPtrs>
struct define_traits {
  template <Traits Constant>
  struct type
    : public define_trait<MemPtrs>::template type<Constant.*MemPtrs>... {
    static constexpr bool _is_traits_type = true;
  };
};

template <typename T, typename = const bool>
struct is_traits_type : std::false_type {};

template <typename T>
struct is_traits_type<T, decltype(T::_is_traits_type)> {
  static constexpr bool value = T::_is_traits_type;
};

template <typename Traits>
constexpr bool is_traits_type_v = is_traits_type<Traits>::value;

}  // namespace unifex::_make_traits
