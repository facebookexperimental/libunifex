#pragma once

#include <unifex/adapt_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/typed_via.hpp>
#include <unifex/via.hpp>

namespace unifex {

template <typename StreamSender, typename Scheduler>
auto via_stream(Scheduler&& scheduler, StreamSender&& stream) {
  return adapt_stream(
      (StreamSender &&) stream,
      [s = (Scheduler &&) scheduler](auto&& sender) mutable {
        return via(cpo::schedule(s), (decltype(sender))sender);
      },
      [s = (Scheduler &&) scheduler](auto&& sender) mutable {
        return typed_via(cpo::schedule(s), (decltype(sender))sender);
      });
}

} // namespace unifex
