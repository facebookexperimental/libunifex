#pragma once

#include <unifex/stream_concepts.hpp>

#include <functional>

namespace unifex {

template <typename StreamSender, typename AdaptFunc>
struct next_adapted_stream {
  StreamSender innerStream_;
  AdaptFunc adapter_;

  auto next() {
    return std::invoke(adapter_, cpo::next(innerStream_));
  }

  auto cleanup() {
    return cpo::cleanup(innerStream_);
  }
};

template <typename StreamSender, typename AdapterFunc>
auto next_adapt_stream(StreamSender&& stream, AdapterFunc&& adapt) {
  return next_adapted_stream<
      std::remove_cvref_t<StreamSender>,
      std::remove_cvref_t<AdapterFunc>>{(StreamSender &&) stream,
                                        (AdapterFunc &&) adapt};
}

} // namespace unifex
