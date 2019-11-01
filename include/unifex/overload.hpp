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

namespace unifex {

namespace detail {

template <typename CPO, typename Sig>
struct overloaded_cpo : CPO {
  constexpr overloaded_cpo() = default;
  constexpr overloaded_cpo(CPO) noexcept {}

  using type_erased_signature_t = Sig;
};

template <typename CPO>
struct base_cpo {
    using type = CPO;
};

template <typename CPO, typename Sig>
struct base_cpo<overloaded_cpo<CPO, Sig>> {
  using type = CPO;
};

template <typename CPO>
using base_cpo_t = typename base_cpo<CPO>::type;

template <typename CPO, typename Sig>
inline constexpr overloaded_cpo<CPO, Sig> overload_{};

} // namespace detail

template <typename Sig, typename CPO>
constexpr auto& overload(CPO) {
  return detail::overload_<CPO, Sig>;
}

} // namespace unifex
