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

#include <unifex/adapt_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/typed_via.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _typed_via_stream {
  struct _fn {
    template (typename StreamSender, typename Scheduler)
        (requires scheduler<Scheduler>)
    auto operator()(Scheduler&& scheduler, StreamSender&& stream) const {
      return adapt_stream(
          (StreamSender &&) stream,
          [s = (Scheduler &&) scheduler](auto&& sender) mutable {
            return typed_via((decltype(sender))sender, s);
          });
    }
    template (typename StreamSender, typename Scheduler)
        (requires scheduler<Scheduler>)
    auto operator()(StreamSender&& stream, Scheduler&& scheduler) const {
      return adapt_stream(
          (StreamSender &&) stream,
          [s = (Scheduler &&) scheduler](auto&& sender) mutable {
            return typed_via((decltype(sender))sender, s);
          });
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
} // namespace _typed_via_stream

inline constexpr _typed_via_stream::_fn typed_via_stream {};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
