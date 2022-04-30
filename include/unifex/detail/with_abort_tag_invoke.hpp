/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *           (c) Lewis Baker
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
#include <unifex/this.hpp>

#include <cstdlib>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace detail {
template <typename Derived, typename CPO, typename Sig>
struct _with_abort_tag_invoke;

template <typename Derived, typename CPO, typename Ret, typename... Args>
struct _with_abort_tag_invoke<Derived, CPO, Ret(Args...)> {
  [[noreturn]] friend Ret
  tag_invoke(CPO, replace_this_t<Args, Derived>...) noexcept {
    std::abort();
  }
};

template <typename Derived, typename CPO, typename Ret, typename... Args>
struct _with_abort_tag_invoke<Derived, CPO, Ret(Args...) noexcept> {
  [[noreturn]] friend Ret
  tag_invoke(CPO, replace_this_t<Args, Derived>...) noexcept {
    std::abort();
  }
};

template <typename Derived, typename CPO>
using with_abort_tag_invoke = _with_abort_tag_invoke<
    Derived,
    base_cpo_t<CPO>,
    typename CPO::type_erased_signature_t>;
}  // namespace detail
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
