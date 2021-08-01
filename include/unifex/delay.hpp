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
#include <unifex/adapt_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/finally.hpp>
#include <unifex/bind_back.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  namespace _delay {
    inline const struct _fn {
      template <typename Stream, typename Scheduler, typename Duration>
      auto operator()(Stream&& stream, Scheduler&& scheduler, Duration&& duration) const {
        return adapt_stream(
            (Stream &&) stream,
            [scheduler = (Scheduler &&) scheduler,
            duration = (Duration &&) duration](auto&& sender) {
              return finally(
                  static_cast<decltype(sender)>(sender),
                  schedule_after(scheduler, duration));
            });
      }
      template <typename Scheduler, typename Duration>
      constexpr auto operator()(Scheduler&& scheduler, Duration&& duration) const
          noexcept(is_nothrow_callable_v<
            tag_t<bind_back>, _fn, Scheduler, Duration>)
          -> bind_back_result_t<_fn, Scheduler, Duration> {
        return bind_back(*this, (Scheduler&&)scheduler, (Duration&&)duration);
      }
    } delay{};
  } // namespace _delay
  using _delay::delay;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
