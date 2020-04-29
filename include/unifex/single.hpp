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

#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/ready_done_sender.hpp>
#include <unifex/scope_guard.hpp>

#include <optional>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _single {
template <typename Sender, typename Receiver>
struct _next_op {
  struct type;
};
template <typename Sender, typename Receiver>
using next_operation = typename _next_op<Sender, remove_cvref_t<Receiver>>::type;

template <typename Sender, typename Receiver>
struct _next_op<Sender, Receiver>::type {
  union {
    Receiver receiver_;
    manual_lifetime<connect_result_t<Sender, Receiver>> innerOp_;
  };
  bool done_;

  template <typename Receiver2>
  explicit type(Receiver2&& receiver)
    : receiver_((Receiver2&&)receiver)
    , done_(true)
  {}

  explicit type(Sender&& sender, Receiver&& receiver)
    : done_(false)
  {
    innerOp_.construct_from([&] {
      return unifex::connect(
          static_cast<Sender&&>(sender), (Receiver&&)receiver);
    });
  }

  ~type() {
    if (done_) {
      receiver_.~Receiver();
    } else {
      innerOp_.destruct();
    }
  }

  void start() noexcept {
    if (done_) {
      unifex::set_done(std::move(receiver_));
    } else {
      unifex::start(innerOp_.get());
    }
  }
};

template <typename Sender>
struct _stream {
  struct type;
};
template <typename Sender>
using stream = typename _stream<remove_cvref_t<Sender>>::type;

template <typename Sender>
struct _stream<Sender>::type {
  std::optional<Sender> sender_;

  struct next_sender {
    std::optional<Sender> sender_;

    template <template <typename...> class Variant,
             template <typename...> class Tuple>
    using value_types = typename Sender::template value_types<Variant, Tuple>;

    template <template <typename...> class Variant>
    using error_types = typename Sender::template error_types<Variant>;

    template <typename Receiver>
    auto connect(Receiver&& receiver) {
      if (sender_) {
        return next_operation<Sender, Receiver>{*std::move(sender_), (Receiver&&)receiver};
      } else {
        return next_operation<Sender, Receiver>{(Receiver&&)receiver};
      }
    }
  };

  friend next_sender tag_invoke(tag_t<next>, type& s) {
    scope_guard g{[&]() noexcept { s.sender_.reset(); }};
    return next_sender{std::move(s.sender_)};
  }

  friend ready_done_sender tag_invoke(tag_t<cleanup>, type&) noexcept {
    return {};
  }

  template <typename Sender2>
  explicit type(Sender2&& sender)
    : sender_(std::in_place, (Sender2&&)sender) {}
};
} // namespace _single

namespace _single_cpo {
  inline const struct _fn {
    template <typename Sender>
    auto operator()(Sender&& sender) const {
      return _single::stream<Sender>{(Sender&&)sender};
    }
  } single{};
} // namespace _single_cpo

using _single_cpo::single;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
