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

#include <unifex/tag_invoke.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/with_query_value.hpp>
#include <unifex/sequence.hpp>

#include <type_traits>

namespace unifex
{
  inline constexpr struct on_cpo {
    template <
        typename Sender,
        typename Scheduler,
        std::enable_if_t<is_tag_invocable_v<on_cpo, Sender, Scheduler>, int> =
            0>
    auto operator()(Sender&& sender, Scheduler&& scheduler) const
        noexcept(is_nothrow_tag_invocable_v<on_cpo, Sender, Scheduler>) {
      return unifex::tag_invoke(
          *this, (Sender &&) sender, (Scheduler &&) scheduler);
    }

    template <
        typename Sender,
        typename Scheduler,
        std::enable_if_t<!is_tag_invocable_v<on_cpo, Sender, Scheduler>, int> =
            0>
    auto operator()(Sender&& sender, Scheduler&& scheduler) const {
      return with_query_value(
          sequence(schedule(), (Sender&&)sender),
          get_scheduler,
          (Scheduler&&)scheduler);
    }
  } on;
  
}  // namespace unifex
