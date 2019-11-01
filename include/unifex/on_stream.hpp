#pragma once

#include <unifex/adapt_stream.hpp>
#include <unifex/on.hpp>
#include <unifex/scheduler_concepts.hpp>

namespace unifex {

template <typename StreamSender, typename Scheduler>
auto on_stream(Scheduler&& scheduler, StreamSender&& stream) {
  return adapt_stream(
      (StreamSender &&) stream,
      [s = (Scheduler &&) scheduler](auto&& sender) mutable {
        return on(cpo::schedule(s), (decltype(sender))sender);
      });
}

} // namespace unifex
