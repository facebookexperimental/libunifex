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
#include <unifex/adapt_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/typed_via.hpp>
#include <unifex/via.hpp>

namespace unifex {
namespace _via_stream_cpo {
  struct _fn {
    template <typename StreamSender, typename Scheduler>
    auto operator()(Scheduler&& scheduler, StreamSender&& stream) const {
      return adapt_stream(
          (StreamSender &&) stream,
          [s = (Scheduler &&) scheduler](auto&& sender) mutable {
            return via(schedule(s), (decltype(sender))sender);
          },
          [s = (Scheduler &&) scheduler](auto&& sender) mutable {
            return typed_via((decltype(sender))sender, s);
          });
    }
  };
} // namespace _via_stream_cpo

UNIFEX_INLINE_VAR constexpr _via_stream_cpo::_fn via_stream {};
} // namespace unifex
