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

  template(typename Receiver2)
    (requires constructible_from<Receiver, Receiver2>)
  explicit type(Receiver2&& receiver)
    : receiver_((Receiver2&&)receiver)
    , done_(true)
  {}

  explicit type(Sender&& sender, Receiver&& receiver)
    : done_(false)
  {
    unifex::activate_union_member_with(innerOp_, [&] {
      return unifex::connect(
          static_cast<Sender&&>(sender), (Receiver&&)receiver);
    });
  }

  ~type() {
    if (done_) {
      receiver_.~Receiver();
    } else {
      unifex::deactivate_union_member(innerOp_);
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
    using value_types = sender_value_types_t<Sender, Variant, Tuple>;

    template <template <typename...> class Variant>
    using error_types = sender_error_types_t<Sender, Variant>;

    static constexpr bool sends_done = true;

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
    constexpr auto operator()() const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn>)
        -> bind_back_result_t<_fn> {
      return bind_back(*this);
    }
  } single{};
} // namespace _single_cpo

using _single_cpo::single;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
