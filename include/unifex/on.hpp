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

#include <unifex/config.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/blocking.hpp>

#include <exception>
#include <type_traits>
#include <utility>

namespace unifex {

template <typename Predecessor, typename Successor>
struct on_sender {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Successor succ_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = typename Successor::template value_types<Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = concat_unique_t<
      typename Predecessor::template error_types<Variant>,
      typename Successor::template error_types<Variant>>;

  friend blocking_kind tag_invoke(
      tag_t<blocking>,
      const on_sender<Predecessor, Successor>& sender) {
    const auto predBlocking = blocking(sender.pred_);
    const auto succBlocking = blocking(sender.succ_);
    if (predBlocking == blocking_kind::never) {
      return blocking_kind::never;
    } else if (
        predBlocking == blocking_kind::always_inline &&
        predBlocking == blocking_kind::always_inline) {
      return blocking_kind::always_inline;
    } else if (
        (predBlocking == blocking_kind::always_inline ||
         predBlocking == blocking_kind::always) &&
        (succBlocking == blocking_kind::always_inline ||
         succBlocking == blocking_kind::always)) {
      return blocking_kind::always;
    } else {
      return blocking_kind::maybe;
    }
  }

  template <typename Receiver>
  struct operation {
    struct successor_receiver {
      operation& op_;

      template <typename... Values>
      void value(Values... values) && noexcept {
        auto& op = op_;
        op.succOp_.destruct();
        unifex::set_value(
            static_cast<Receiver&&>(op.receiver_), (Values &&) values...);
      }

      template <typename Error>
      void error(Error error) && noexcept {
        auto& op = op_;
        op.succOp_.destruct();
        unifex::set_error(
            static_cast<Receiver&&>(op.receiver_), (Error &&) error);
      }

      void done() && noexcept {
        auto& op = op_;
        op.succOp_.destruct();
        unifex::set_done(static_cast<Receiver&&>(op.receiver_));
      }

      template <
          typename CPO,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const successor_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.op_.receiver_));
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const successor_receiver& r,
          Func&& func) {
        std::invoke(func, r.op_.receiver_);
      }
    };

    struct predecessor_receiver {
      operation& op_;

      template <typename... Values>
      void value(Values&&...) noexcept {
        auto& op = op_;
        op.predOp_.destruct();
        try {
          op.succOp_.construct_from([&]() {
            return unifex::connect(
                static_cast<Successor&&>(op.succ_),
                successor_receiver{op});
          });
          unifex::start(op.succOp_.get());
        } catch (...) {
          unifex::set_error(
              static_cast<Receiver&&>(op.receiver_),
              std::current_exception());
        }
      }

      template <typename Error>
      void error(Error error) noexcept {
        auto& op = op_;
        op.predOp_.destruct();
        unifex::set_error(
            static_cast<Receiver&&>(op.receiver_), (Error &&) error);
      }

      void done() noexcept {
        auto& op = op_;
        op.predOp_.destruct();
        unifex::set_done(static_cast<Receiver&&>(op.receiver_));
      }

      template <
          typename CPO,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const predecessor_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.op_.receiver_));
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const predecessor_receiver& r,
          Func&& func) {
        std::invoke(func, r.op_.receiver_);
      }
    };

    template <typename Receiver2>
    explicit operation(
        Predecessor&& pred,
        Successor&& succ,
        Receiver2&& receiver)
        : succ_((Successor &&) succ)
        , receiver_((Receiver2&&)receiver) {
      predOp_.construct_from([&] {
        return unifex::connect(
            static_cast<Predecessor&&>(pred),
            predecessor_receiver{*this});
      });
    }

    ~operation() {
      if (!started_)
        predOp_.destruct();
    }

    Successor succ_;
    Receiver receiver_;
    union {
      manual_lifetime<operation_t<Predecessor, predecessor_receiver>>
          predOp_;
      manual_lifetime<operation_t<Successor, successor_receiver>>
          succOp_;
    };
    bool started_ = false;

    void start() noexcept {
      started_ = true;
      unifex::start(predOp_.get());
    }
  };

  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
    return operation<std::remove_cvref_t<Receiver>>{
        (Predecessor &&) pred_, (Successor &&) succ_, (Receiver &&) receiver};
  }
};

template <typename Predecessor, typename Successor>
auto on(Predecessor&& predecessor, Successor&& successor) {
  return on_sender<
      std::remove_cvref_t<Predecessor>,
      std::remove_cvref_t<Successor>>{(Predecessor &&) predecessor,
                                      (Successor &&) successor};
}

} // namespace unifex
