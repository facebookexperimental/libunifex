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
        (requires invocable<TargetComposeFn, Target>)
    friend auto operator|(Target&& target, TargetComposeFn&& fn) 
      noexcept(
        std::is_nothrow_invocable_v<TargetComposeFn, Target>) 
      -> std::invoke_result_t<TargetComposeFn, Target> {
        return fn((Target&&) target);
    }
  };
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
