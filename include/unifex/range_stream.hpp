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

#include <unifex/blocking.hpp>
#include <unifex/ready_done_sender.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stream_concepts.hpp>

#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _range {
struct stream;

template <typename Receiver>
struct _op {
  struct type;
};
template <typename Receiver>
using operation = typename _op<remove_cvref_t<Receiver>>::type;

template <typename Receiver>
struct _op<Receiver>::type {
  stream& stream_;
  Receiver receiver_;

  void start() noexcept;
};

struct next_sender {
  stream& stream_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<int>>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = true;

  friend constexpr auto tag_invoke(
      tag_t<blocking>,
      const stream&) noexcept {
    return blocking_kind::always_inline;
  }

  template <typename Receiver>
  operation<Receiver> connect(Receiver&& receiver) && {
    return operation<Receiver>{stream_, (Receiver &&) receiver};
  }
  template <typename Receiver>
  void connect(Receiver&& receiver) const& = delete;
};

struct stream {
  int next_;
  int max_;

  explicit stream(int max) : next_(0), max_(max) {}
  explicit stream(int start, int max) : next_(start), max_(max) {}

  friend next_sender tag_invoke(tag_t<next>, stream& s) noexcept {
    return next_sender{s};
  }

  friend ready_done_sender tag_invoke(tag_t<cleanup>, stream&) noexcept {
    return {};
  }
};

template <typename Receiver>
void _op<Receiver>::type::start() noexcept {
  if (stream_.next_ < stream_.max_) {
    unifex::set_value(std::move(receiver_), stream_.next_++);
  } else {
    unifex::set_done(std::move(receiver_));
  }
}
} // namespace _range

using range_stream = _range::stream;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
