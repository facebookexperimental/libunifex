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

#include <unifex/config.hpp>
#include <unifex/std_concepts.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
  struct enable_operator_composition {
    template (typename Target, typename TargetComposeFn)
        (requires callable<TargetComposeFn, Target>)
    friend auto operator|(Target&& target, TargetComposeFn&& fn) 
      noexcept(
        is_nothrow_callable_v<TargetComposeFn, Target>) 
      -> callable_result_t<TargetComposeFn, Target> {
        return ((TargetComposeFn&&)fn)((Target&&) target);
    }
  };
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
