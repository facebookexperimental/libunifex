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
#include <unifex/tag_invoke.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/transform.hpp>
#include <unifex/submit.hpp>

#include <exception>

namespace unifex
{
  namespace _execute
  {
    struct default_execute_receiver {
      void set_done() && noexcept {}
      template<typename Error>
      [[noreturn]] void set_error(Error&&) && noexcept {
        std::terminate();
      }
      void set_value() && noexcept {}
    };

    UNIFEX_INLINE_VAR constexpr struct _fn {
      template<typename Scheduler, typename Func>
      void operator()(Scheduler&& s, Func&& func) const {
        if constexpr (is_tag_invocable_v<_fn, Scheduler, Func>) {
          unifex::tag_invoke(*this, (Scheduler&&)s, (Func&&)func);
        } else {
          // Default implementation.
          return submit(
            transform(schedule((Scheduler&&)s), (Func&&)func),
            default_execute_receiver{});
        }
      }
    } execute{};
  } // namespace _execute
  using _execute::execute;
} // namespace unifex
