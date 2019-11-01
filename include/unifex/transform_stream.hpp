#pragma once

#include <unifex/next_adapt_stream.hpp>
#include <unifex/transform.hpp>

#include <functional>

namespace unifex {

template <typename StreamSender, typename Func>
auto transform_stream(StreamSender&& stream, Func&& func) {
  return next_adapt_stream(
      (StreamSender &&) stream, [func = (Func &&) func](auto&& sender) mutable {
        return transform((decltype(sender))sender, std::ref(func));
      });
}

} // namespace unifex
