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

#include <unifex/finally.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/bind_back.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _typed_via {
  struct _fn {
    template(typename Source, typename Scheduler)
        (requires tag_invocable<_fn, Source, Scheduler>)
    auto operator()(Source&& source, Scheduler&& scheduler) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Source, Scheduler>)
            -> tag_invoke_result_t<_fn, Source, Scheduler> {
      return tag_invoke(
          *this,
          static_cast<Source&&>(source),
          static_cast<Scheduler&&>(scheduler));
    }

    template(typename Source, typename Scheduler)
        (requires (!tag_invocable<_fn, Source, Scheduler>))
    auto operator()(Source&& source, Scheduler&& scheduler) const
        noexcept(noexcept(finally(
            static_cast<Source&&>(source),
            schedule(static_cast<Scheduler&&>(scheduler)))))
            -> decltype(finally(
                static_cast<Source&&>(source),
                schedule(static_cast<Scheduler&&>(scheduler)))) {
      return finally(
          static_cast<Source&&>(source),
          schedule(static_cast<Scheduler&&>(scheduler)));
    }
    template(typename Scheduler)
        (requires scheduler<Scheduler>)
    constexpr auto operator()(Scheduler&& scheduler) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, Scheduler>)
        -> bind_back_result_t<_fn, Scheduler> {
      return bind_back(*this, (Scheduler&&)scheduler);
    }
  };
} // namespace _typed_via

inline constexpr _typed_via::_fn typed_via {};
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
