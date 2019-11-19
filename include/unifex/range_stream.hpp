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

#include <unifex/blocking.hpp>
#include <unifex/ready_done_sender.hpp>
#include <unifex/receiver_concepts.hpp>

#include <type_traits>
#include <utility>

namespace unifex {

struct range_stream {
  int next_;
  int max_;

  explicit range_stream(int max) : next_(0), max_(max) {}
  explicit range_stream(int start, int max) : next_(start), max_(max) {}

  struct next_sender {
    range_stream& stream_;

    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<int>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    friend constexpr blocking_kind tag_invoke(
        tag_t<blocking>,
        const range_stream&) noexcept {
      return blocking_kind::always_inline;
    }

    template <typename Receiver>
    struct operation {
      range_stream& stream_;
      Receiver receiver_;

      void start() noexcept {
        if (stream_.next_ < stream_.max_) {
          cpo::set_value(std::move(receiver_), stream_.next_++);
        } else {
          cpo::set_done(std::move(receiver_));
        }
      }
    };

    template <typename Receiver>
    operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
      return operation<std::remove_cvref_t<Receiver>>{stream_,
                                                      (Receiver &&) receiver};
    }
  };

  next_sender next() & {
    return next_sender{*this};
  }

  ready_done_sender cleanup() & {
    return {};
  }
};

} // namespace unifex
