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

#include <utility>
#include <unifex/tag_invoke.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _overload {

template <typename CPO, typename Sig>
struct _cpo_t {
  struct type;
};

// This type will have the associated namespaces
// of CPO (by inheritance) but not those of Sig.
template <typename CPO, typename Sig>
struct _cpo_t<CPO, Sig>::type : CPO {
  constexpr type() = default;
  constexpr type(CPO) noexcept {}

  using base_cpo_t = CPO;
  using type_erased_signature_t = Sig;
};

template <typename CPO, typename Enable = void>
struct base_cpo {
  using type = CPO;
};

template <typename CPO>
struct base_cpo<CPO, std::void_t<typename CPO::base_cpo_t>> {
  using type = typename CPO::base_cpo_t;
};

template <typename CPO>
using base_cpo_t = typename base_cpo<CPO>::type;

template <typename CPO, typename Sig>
inline constexpr typename _cpo_t<CPO, Sig>::type _cpo{};

template <typename Sig>
struct _sig {};

} // namespace _overload

template <typename Sig>
inline constexpr _overload::_sig<Sig> const sig{};

template <auto& CPO, typename Sig>
using overload_t = typename _overload::_cpo_t<tag_t<CPO>, Sig>::type;

template <typename Sig, typename CPO>
constexpr typename _overload::_cpo_t<CPO, Sig>::type const&
overload(CPO const&, _overload::_sig<Sig> = {}) noexcept {
  return _overload::_cpo<CPO, Sig>;
}

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
