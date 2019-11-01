#pragma once

#include <unifex/stream_concepts.hpp>

#include <functional>
#include <type_traits>

namespace unifex {

template <
    typename StreamSender,
    typename NextAdaptFunc,
    typename CleanupAdaptFunc>
struct adapted_stream {
  StreamSender innerStream_;
  NextAdaptFunc nextAdapter_;
  CleanupAdaptFunc cleanupAdapter_;

  auto next() {
    return std::invoke(nextAdapter_, cpo::next(innerStream_));
  }

  auto cleanup() {
    return std::invoke(cleanupAdapter_, cpo::cleanup(innerStream_));
  }
};

template <typename StreamSender, typename AdaptFunc>
struct both_adapted_stream {
  StreamSender innerStream_;
  AdaptFunc adapter_;

  auto next() {
    return std::invoke(adapter_, cpo::next(innerStream_));
  }

  auto cleanup() {
    return std::invoke(adapter_, cpo::cleanup(innerStream_));
  }
};

template <typename StreamSender, typename AdapterFunc>
auto adapt_stream(StreamSender&& stream, AdapterFunc&& adapt) {
  return both_adapted_stream<
      std::remove_cvref_t<StreamSender>,
      std::remove_cvref_t<AdapterFunc>>{(StreamSender &&) stream,
                                        (AdapterFunc &&) adapt};
}

template <
    typename StreamSender,
    typename NextAdapterFunc,
    typename CleanupAdapterFunc>
auto adapt_stream(
    StreamSender&& stream,
    NextAdapterFunc&& adaptNext,
    CleanupAdapterFunc&& adaptCleanup) {
  return adapted_stream<
      std::remove_cvref_t<StreamSender>,
      std::remove_cvref_t<NextAdapterFunc>,
      std::remove_cvref_t<CleanupAdapterFunc>>{(StreamSender &&) stream,
                                               (NextAdapterFunc &&) adaptNext,
                                               (CleanupAdapterFunc &&)
                                                   adaptCleanup};
}

} // namespace unifex
