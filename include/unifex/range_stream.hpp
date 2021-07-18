/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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
#include <unifex/stream_concepts.hpp>

#include <type_traits>
#include <utility>
#include <ranges>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _range {

template <std::ranges::range Range>
struct stream;

template <typename Receiver, std::ranges::range Range>
struct _op {
  struct type;
};
template <typename Receiver, std::ranges::range Range>
using operation = typename _op<remove_cvref_t<Receiver>, Range>::type;

template <typename Receiver, std::ranges::range Range>
struct _op<Receiver, Range>::type {

  stream<Range>& stream_;
  Receiver receiver_;

  void start() noexcept;
};

template <std::ranges::range Range>
struct next_sender {
  stream<Range>& stream_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<int>>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = true;

  friend constexpr blocking_kind tag_invoke(
      tag_t<blocking>,
      const stream<Range>&) noexcept {
    return blocking_kind::always_inline;
  }

  template <typename Receiver>
  operation<Receiver, Range> connect(Receiver&& receiver) && {
    return operation<Receiver, Range>{stream_, (Receiver &&) receiver};
  }
  template <typename Receiver>
  void connect(Receiver&& receiver) const& = delete;
};

template <std::ranges::range Range>
struct stream {
  using iterator_t = std::ranges::iterator_t<Range>;
  using sentinel_t = std::ranges::sentinel_t<Range>;

  iterator_t begin_;
  sentinel_t end_;

  explicit stream(Range&& r) : begin_(std::begin(r)), end_(std::end(r)) {}
  explicit stream(iterator_t begin, sentinel_t end) : begin_(begin), end_(end) {}

  friend next_sender<Range> tag_invoke(tag_t<next>, stream<Range>& s) noexcept {
    return next_sender{s};
  }

  friend ready_done_sender tag_invoke(tag_t<cleanup>, stream&) noexcept {
    return {};
  }
};

template <typename Receiver, std::ranges::range Range>
void _op<Receiver, Range>::type::start() noexcept {
  if (stream_.begin_ < stream_.end_) {
    unifex::set_value(std::move(receiver_), *stream_.begin_++);
  } else {
    unifex::set_done(std::move(receiver_));
  }
}
} // namespace _range

template <std::ranges::range Range>
using range_stream = _range::stream<Range>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
