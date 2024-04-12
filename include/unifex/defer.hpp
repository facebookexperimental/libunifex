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

#include <unifex/config.hpp>
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _defer {
inline const struct _fn {
  template(typename Callable)(
      requires std::is_invocable_v<Callable> AND
          sender<std::invoke_result_t<Callable>>) constexpr auto
  operator()(Callable&& invocable) const {
    return let_value(just(), (Callable &&) invocable);
  }
} defer{};
}  // namespace _defer
using _defer::defer;
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
