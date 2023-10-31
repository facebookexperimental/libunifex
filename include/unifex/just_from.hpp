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
#include <unifex/then.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _just_from {
inline const struct _fn {
  template(typename Callable)        //
      (requires callable<Callable>)  //
      constexpr auto
      operator()(Callable&& callable) const {
    return then(just(), (Callable &&) callable);
  }
} just_from{};
}  // namespace _just_from
using _just_from::just_from;
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
