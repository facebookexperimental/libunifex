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

#include <unifex/scheduler_concepts.hpp>
#include <unifex/then.hpp>
#include <unifex/typed_via.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _then_execute_cpo {
  struct _fn {
    template <typename Scheduler, typename Predecessor, typename Func>
    auto operator()(Scheduler&& s, Predecessor&& p, Func&& f) const {
      return then(
          typed_via((Predecessor &&) p, (Scheduler&&)s),
          (Func &&) f);
    }
  };
} // namespace _then_execute_cpo
inline constexpr _then_execute_cpo::_fn then_execute {};
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
