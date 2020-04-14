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
#include <unifex/on.hpp>
#include <unifex/scheduler_concepts.hpp>

namespace unifex {
namespace _on_stream {
  UNIFEX_INLINE_VAR constexpr struct _fn {
    template <typename StreamSender, typename Scheduler>
    auto operator()(Scheduler&& scheduler, StreamSender&& stream) const {
      return adapt_stream(
          (StreamSender &&) stream,
          [s = (Scheduler &&) scheduler](auto&& sender) mutable {
            return on((decltype(sender))sender, s);
          });
    }
  } on_stream {};
} // namespace _on_stream

using _on_stream::on_stream;

} // namespace unifex
