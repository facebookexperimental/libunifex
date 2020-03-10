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

#include <cstddef>
#include <type_traits>

namespace unifex {

// use to nip types in the bud
// reduces length of types in debug output 
// in some compilers.
// also reduces length of types in tools 
// like -ftime-trace in clang
namespace nip {
  template<class T>
  auto type(T&&) {
    struct nip {
      using type = T;
    };
    return nip{};
  }
} // namespace nip

template<class T>
using nip_t = decltype(nip::type(std::declval<T>()));
template<class NipT>
using unnip_t = typename NipT::type;

template<class T>
using identity_t = T;
template<class T>
using add_cvref_t = std::add_const_t<std::add_lvalue_reference_t<T>>;

} //namespace unifex 
