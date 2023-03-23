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

#include <unifex/tag_invoke.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/with_query_value.hpp>
#include <unifex/sequence.hpp>
#include <unifex/bind_back.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _on {
  inline const struct _fn {
    template(typename Scheduler, typename Sender)
        (requires sender<Sender> AND scheduler<Scheduler> AND //
          tag_invocable<_fn, Scheduler, Sender>)
    auto operator()(Scheduler&& scheduler, Sender&& sender) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Scheduler, Sender>)
        -> tag_invoke_result_t<_fn, Scheduler, Sender> {
      return unifex::tag_invoke(
          _fn{}, (Scheduler &&) scheduler, (Sender &&) sender);
    }

    template(typename Scheduler, typename Sender)
        (requires sender<Sender> AND scheduler<Scheduler> AND //
          (!tag_invocable<_fn, Scheduler, Sender>))
    auto operator()(Scheduler&& scheduler, Sender&& sender) const {
      auto scheduleSender = schedule(scheduler);
      return sequence(
        std::move(scheduleSender),
        with_query_value(
          (Sender&&) sender,
          get_scheduler,
          (Scheduler&&) scheduler));
    }
  } on{};
} // namespace _on

using _on::on;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
