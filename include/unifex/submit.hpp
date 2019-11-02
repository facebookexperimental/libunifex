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
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/tag_invoke.hpp>

namespace unifex {

template <typename Sender, typename Receiver>
class submitted_operation {
  class wrapped_receiver {
    submitted_operation* op_;

  public:
    explicit wrapped_receiver(submitted_operation* op) noexcept : op_(op) {}

    template <typename... Values>
    void value(Values&&... values) && noexcept {
      cpo::set_value(std::move(op_->receiver_), (Values &&) values...);
      delete op_;
    }

    template <typename Error>
    void error(Error&& error) && noexcept {
      cpo::set_error(std::move(op_->receiver_), (Error &&) error);
      delete op_;
    }

    void done() && noexcept {
      cpo::set_done(std::move(op_->receiver_));
      delete op_;
    }

    template <
        typename CPO,
        std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
    friend auto tag_invoke(CPO cpo, const wrapped_receiver& r) noexcept(
        std::is_nothrow_invocable_v<CPO, const Receiver&>)
        -> std::invoke_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.op_->receiver_));
    }

    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const wrapped_receiver& r,
        Func&& func) {
      std::invoke(func, r.op_->receiver_);
    }
  };

public:
  template <typename Receiver2>
  submitted_operation(Sender&& sender, Receiver2&& receiver)
      : receiver_((Receiver2 &&) receiver),
        inner_(cpo::connect((Sender &&) sender, wrapped_receiver{this}))
      {}

  void start() & noexcept {
    cpo::start(inner_);
  }

private:
  Receiver receiver_;
  operation_t<Sender, wrapped_receiver> inner_;
};

inline constexpr struct submit_cpo {
  template<typename Sender, typename Receiver>
  void operator()(Sender&& sender, Receiver&& receiver) const {
    if constexpr (is_tag_invocable_v<submit_cpo, Sender, Receiver>) {
      static_assert(
        std::is_same_v<tag_invoke_result_t<submit_cpo, Sender, Receiver>>,
        "Customisations of submit() must have a void return value");
      unifex::tag_invoke(*this, (Sender&&)sender, (Receiver&&)receiver);
    } else {
      // Default implementation in terms of connect/start
      switch (cpo::blocking(sender)) {
        case blocking_kind::always:
        case blocking_kind::always_inline:
        {
          // The sender will complete synchronously so we can avoid allocating the
          // state on the heap.
          auto op = cpo::connect((Sender &&) sender, (Receiver &&) receiver);
          cpo::start(op);
        }
        default:
        {
          // Otherwise need to heap-allocate the operation-state
          auto* op = new submitted_operation<Sender, std::remove_cvref_t<Receiver>>(
            (Sender &&) sender, (Receiver &&) receiver);
          op->start();
        }
      }
    }
  }
} submit;

} // namespace unifex
