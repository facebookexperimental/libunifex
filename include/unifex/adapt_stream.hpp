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
