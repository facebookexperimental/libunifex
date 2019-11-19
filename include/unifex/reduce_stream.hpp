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
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace unifex {

template <typename StreamSender, typename State, typename ReducerFunc>
struct reduce_stream_sender {
  StreamSender stream_;
  State initialState_;
  ReducerFunc reducer_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<State>>;

  template <template <typename...> class Variant>
  using error_types = concat_unique_t<
      typename next_sender_t<StreamSender>::template error_types<Variant>,
      typename cleanup_sender_t<StreamSender>::template error_types<
          append_unique<Variant, std::exception_ptr>::template apply>>;

  template <typename Receiver>
  struct operation {
    struct error_cleanup_receiver {
      operation& op_;
      std::exception_ptr ex_;

      // No value() in cleanup receiver

      template <typename Error>
      void error(Error error) noexcept {
        auto& op = op_;
        op.errorCleanup_.destruct();
        cpo::set_error(static_cast<Receiver&&>(op.receiver_), (Error &&) error);
      }

      void done() noexcept {
        auto& op = op_;
        auto ex = std::move(ex_);
        op.errorCleanup_.destruct();
        cpo::set_error(static_cast<Receiver&&>(op.receiver_), std::move(ex));
      }

      template <
          typename CPO,
          std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const error_cleanup_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.op_.receiver_));
      }

      friend unstoppable_token tag_invoke(tag_t<get_stop_token>, const error_cleanup_receiver& r) noexcept {
        return {};
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const error_cleanup_receiver& r,
          Func&& func) {
        std::invoke(func, r.op_.receiver_);
      }
    };

    struct done_cleanup_receiver {
      operation& op_;

      template <typename Error>
      void error(Error error) && noexcept {
        auto& op = op_;
        op.doneCleanup_.destruct();
        cpo::set_error(static_cast<Receiver&&>(op.receiver_), (Error &&) error);
      }

      void done() && noexcept {
        auto& op = op_;
        op.doneCleanup_.destruct();
        cpo::set_value(
            static_cast<Receiver&&>(op.receiver_),
            std::forward<State>(op.state_));
      }

      template <
          typename CPO,
          std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const done_cleanup_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.op_.receiver_));
      }

      friend unstoppable_token tag_invoke(tag_t<get_stop_token>, const done_cleanup_receiver& r) noexcept {
        return {};
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const done_cleanup_receiver& r,
          Func&& func) {
        std::invoke(func, r.op_.receiver_);
      }
    };

    struct next_receiver {
      operation& op_;

      template <
          typename CPO,
          std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const next_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.op_.receiver_));
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const next_receiver& r,
          Func&& func) {
        std::invoke(func, r.op_.receiver_);
      }

      template <typename... Values>
      void value(Values... values) && noexcept {
        auto& op = op_;
        op.next_.destruct();
        try {
          op.state_ = std::invoke(
              op.reducer_,
              std::forward<State>(op.state_),
              (Values &&) values...);
          op.next_.construct_from([&]() {
            return unifex::connect(next(op.stream_), next_receiver{op});
          });
          unifex::start(op.next_.get());
        } catch (...) {
          op.errorCleanup_.construct_from([&] {
            return unifex::connect(
                cleanup(op.stream_),
                error_cleanup_receiver{op, std::current_exception()});
          });
          unifex::start(op.errorCleanup_.get());
        }
      }

      void done() && noexcept {
        auto& op = op_;
        op.next_.destruct();
        op.doneCleanup_.construct_from([&]() {
          return unifex::connect(
              cleanup(op.stream_), done_cleanup_receiver{op});
        });
        unifex::start(op.doneCleanup_.get());
      }

      void error(std::exception_ptr ex) && noexcept {
        auto& op = op_;
        op.next_.destruct();
        op.errorCleanup_.construct_from([&]() {
          return unifex::connect(
              cleanup(op.stream_),
              error_cleanup_receiver{op, std::move(ex)});
        });
        unifex::start(op.errorCleanup_.get());
      }

      template <typename Error>
      void error(Error&& e) && noexcept {
        std::move(*this).error(std::make_exception_ptr((Error &&) e));
      }
    };

    UNIFEX_NO_UNIQUE_ADDRESS StreamSender stream_;
    UNIFEX_NO_UNIQUE_ADDRESS State state_;
    UNIFEX_NO_UNIQUE_ADDRESS ReducerFunc reducer_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    union {
      manual_lifetime<next_operation_t<StreamSender, next_receiver>> next_;
      manual_lifetime<cleanup_operation_t<StreamSender, error_cleanup_receiver>>
          errorCleanup_;
      manual_lifetime<cleanup_operation_t<StreamSender, done_cleanup_receiver>>
          doneCleanup_;
    };

    template <typename Receiver2>
    explicit operation(
        StreamSender&& stream,
        State&& state,
        ReducerFunc&& reducer,
        Receiver2&& receiver)
        : stream_(std::forward<StreamSender>(stream)),
          state_(std::forward<State>(state)),
          reducer_(std::forward<ReducerFunc>(reducer)),
          receiver_(std::forward<Receiver2>(receiver)) {}

    ~operation() {}

    void start() noexcept {
      try {
        next_.construct_from([&]() {
          return unifex::connect(next(stream_), next_receiver{*this});
        });
        unifex::start(next_.get());
      } catch (...) {
        cpo::set_error(
            static_cast<Receiver&&>(receiver_), std::current_exception());
      }
    }
  };

  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
    return operation<std::remove_cvref_t<Receiver>>{(StreamSender &&) stream_,
                                                    (State &&) initialState_,
                                                    (ReducerFunc &&) reducer_,
                                                    (Receiver &&) receiver};
  }
};

template <typename StreamSender, typename State, typename ReducerFunc>
auto reduce_stream(
    StreamSender&& stream,
    State&& initialState,
    ReducerFunc&& reducer) {
  return reduce_stream_sender<
      std::remove_cvref_t<StreamSender>,
      std::remove_cvref_t<State>,
      std::remove_cvref_t<ReducerFunc>>{(StreamSender &&) stream,
                                        (State &&) initialState,
                                        (ReducerFunc &&) reducer};
}

} // namespace unifex
