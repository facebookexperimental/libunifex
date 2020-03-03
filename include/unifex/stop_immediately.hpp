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
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>

#include <atomic>
#include <exception>
#include <utility>
#include <type_traits>
#include <cassert>

namespace unifex {

template<typename SourceStream, typename... Values>
struct stop_immediately_stream {
 private:
  enum class state {
    not_started,
    source_next_completed,
    source_next_active,
    source_next_active_stream_stopped,
    source_next_active_cleanup_requested,
    cleanup_completed
  };

  struct cleanup_operation_base {
    virtual void start_cleanup() noexcept = 0;
  };

  struct next_receiver_base {
    virtual void set_value(Values&&... values) && noexcept = 0;
    virtual void set_done() && noexcept = 0;
    virtual void set_error(std::exception_ptr ex) && noexcept = 0;
  };

  struct cancel_next_callback {
    stop_immediately_stream& stream_;

    void operator()() noexcept {
      auto oldState = stream_.state_.load(std::memory_order_acquire);
      if (oldState == state::source_next_active) {
        // We may be racing with the next() operation completing on another
        // thread so we need to use a compare_exchange here to decide the
        // race.
        // Note that the callback destructor is run when we receive
        // the next() operation completion signal before delivering the signal
        // to the true receiver. The destructor will will block waiting for
        // this method to return and so we are guaranteed that there will be
        // no further call to next() or to cleanup() before we return here.
        // The only concurrent state transition can be from
        // 'source_next_active' to 'idle' and there will be no further state
        // changes until we return.
        // Thus it should be safe to use 'relaxed' memory access for the
        // compare-exchange below since we have already synchronised with the
        // 'acquire' operation above.
        if (stream_.state_.compare_exchange_strong(
              oldState,
              state::source_next_active_stream_stopped,
              std::memory_order_relaxed)) {
          // Successfully acquired ownership over the receiver.
          // Send the 'done' signal immediately to signal the end of the
          // sequence and also send the stop signal to the still-running
          // next() operation.
          stream_.stopSource_.request_stop();
          auto receiver = std::exchange(stream_.nextReceiver_, nullptr);
          assert(receiver != nullptr);
          std::move(*receiver).set_done();
        } else {
          assert(oldState == state::source_next_completed);
        }
      } else {
        assert(oldState == state::source_next_completed);
      }
    }
  };

  struct next_receiver {
    stop_immediately_stream& stream_;

    inplace_stop_source& get_stop_source() const {
      return stream_.stopSource_;
    }

    // Note, parameters passed by value here just in case we are passed
    // references to values owned by the operation object that we will be
    // destroying before passing the values along to the next receiver.
    void set_value(Values... values) && noexcept {
      handle_signal([&](next_receiver_base* receiver) noexcept {
        try {
          std::move(*receiver).set_value((Values&&)values...);
        } catch (...) {
          std::move(*receiver).set_error(std::current_exception());
        }
      });
    }

    void set_done() && noexcept {
      handle_signal([](next_receiver_base* receiver) noexcept {
        std::move(*receiver).set_done();
      });
    }

    template<typename Error>
    void set_error(Error&& error) && noexcept {
      std::move(*this).set_error(std::make_exception_ptr((Error&&)error));
    }

    void set_error(std::exception_ptr ex) && noexcept {
      auto& nextError = stream_.nextError_;
      nextError = std::move(ex);
      handle_signal([&](next_receiver_base* receiver) noexcept {
        std::move(*receiver).set_error(std::exchange(nextError, {}));
      });
    }

    template<typename Func>
    void handle_signal(Func deliverSignalTo) noexcept {
      auto& stream = stream_;
      stream.nextOp_.destruct();

      auto oldState = stream.state_.load(std::memory_order_acquire);

      if (oldState == state::source_next_active) {
        if (stream.state_.compare_exchange_strong(
              oldState, state::source_next_completed,
              std::memory_order_relaxed)) {
          // We acquired ownership of the receiver before it was cancelled.
          auto* receiver = std::exchange(stream.nextReceiver_, nullptr);
          assert(receiver != nullptr);
          deliverSignalTo(receiver);
          return;
        }
      }

      if (oldState == state::source_next_active_stream_stopped) {
        if (stream.state_.compare_exchange_strong(
              oldState, state::source_next_completed,
              std::memory_order_release,
              std::memory_order_acquire)) {
          // Successfully signalled that 'next' completed before 'cleanup'
          // operation started. Discard this signal without forwarding it on.
          return;
        }
      }

      // Otherwise, cleanup() was requested before this operation completed.
      // We are responsible for starting cleanup now that next() has finished.

      assert(oldState == state::source_next_active_cleanup_requested);
      assert(stream_.cleanupOp_ != nullptr);
      stream_.cleanupOp_->start_cleanup();
    }

    friend inplace_stop_token tag_invoke(
        tag_t<get_stop_token>, const next_receiver& r) noexcept {
      return r.get_stop_source().get_token();
    }

    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const next_receiver& r,
        Func&& func) {
      std::invoke(func, r.op_->receiver_);
    }
  };

  struct next_sender {
    stop_immediately_stream& stream_;

    template<template<typename...> class Variant,
             template<typename...> class Tuple>
    using value_types =
      typename next_sender_t<SourceStream>::template value_types<Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types =
      typename next_sender_t<SourceStream>::template error_types<Variant>;

    template<typename Receiver>
    struct operation {

      struct concrete_receiver final : next_receiver_base {
        operation& op_;

        explicit concrete_receiver(operation& op)
        : op_(op)
        {}

        void set_value(Values&&... values) && noexcept final {
          op_.stopCallback_.destruct();
          unifex::set_value(std::move(op_.receiver_), (Values&&)values...);
        }

        void set_done() && noexcept final {
          op_.stopCallback_.destruct();
          unifex::set_done(std::move(op_.receiver_));
        }

        void set_error(std::exception_ptr ex) && noexcept final {
          op_.stopCallback_.destruct();
          unifex::set_error(std::move(op_.receiver_), std::move(ex));
        }
      };

      stop_immediately_stream& stream_;
      concrete_receiver concreteReceiver_;
      UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
      UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<
        typename stop_token_type_t<Receiver&>::
        template callback_type<cancel_next_callback>>
          stopCallback_;

      using ST = stop_token_type_t<Receiver&>;

      template<typename Receiver2>
      explicit operation(stop_immediately_stream& stream,
                         Receiver2&& receiver)
      : stream_(stream)
      , concreteReceiver_(*this)
      , receiver_{(Receiver2&&)receiver}
      {}

      void start() noexcept {
        auto stopToken = get_stop_token(receiver_);
        if (stopToken.stop_requested()) {
            unifex::set_done(std::move(receiver_));
            return;
        }

        static_assert(
          std::is_same_v<decltype(stopToken), ST>);

        try {
          stream_.nextOp_.construct_from([&] {
            return unifex::connect(
              next(stream_.source_),
              next_receiver{stream_});
          });
          stream_.nextReceiver_ = &concreteReceiver_;
          stream_.state_.store(
            state::source_next_active, std::memory_order_relaxed);
          try {
            stopCallback_.construct(
              std::move(stopToken),
              cancel_next_callback{stream_});
            unifex::start(stream_.nextOp_.get());
          } catch (...) {
            stream_.nextReceiver_ = nullptr;
            stream_.nextOp_.destruct();
            stream_.state_.store(
              state::source_next_completed, std::memory_order_relaxed);
            unifex::set_error(std::move(receiver_), std::current_exception());
          }
        } catch (...) {
          stream_.state_.store(
            state::source_next_completed, std::memory_order_relaxed);
          unifex::set_error(std::move(receiver_), std::current_exception());
        }
      }
    };

    template<typename Receiver>
    operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
      return operation<std::remove_cvref_t<Receiver>>{
        stream_,
        (Receiver&&)receiver};
    }
  };

  struct cleanup_sender {
    stop_immediately_stream& stream_;

    template<template<typename...> class Variant,
             template<typename...> class Tuple>
    using value_types = Variant<>;

    template<template<typename...> class Variant>
    using error_types = typename concat_type_lists_unique_t<
      typename cleanup_sender_t<SourceStream>::template error_types<type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

    template<typename Receiver>
    struct operation final : cleanup_operation_base {

      struct receiver_wrapper {
        operation& op_;

        void set_done() && noexcept {
          auto& op = op_;
          op.cleanupOp_.destruct();

          if (op.stream_.nextError_) {
            unifex::set_error(
                std::move(op.receiver_), std::move(op.stream_.nextError_));
          } else {
            unifex::set_done(std::move(op.receiver_));
          }
        }

        template<typename Error>
        void set_error(Error&& error) && noexcept {
          auto& op = op_;
          op.cleanupOp_.destruct();

          // Prefer sending the error from the next(source_) rather than
          // the error from cleanup(source_).
          if (op.stream_.nextError_) {
            unifex::set_error(
              std::move(op.receiver_), std::move(op.stream_.nextError_));
          } else {
            unifex::set_error(std::move(op.receiver_), (Error&&)error);
          }
        }
      };

      stop_immediately_stream& stream_;
      UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

      manual_lifetime<cleanup_operation_t<SourceStream, receiver_wrapper>>
          cleanupOp_;

      template<typename Receiver2>
      explicit operation(stop_immediately_stream& stream,
                         Receiver2&& receiver)
      : stream_(stream)
      , receiver_((Receiver2&&)receiver)
      {}

      void start() noexcept {
        auto oldState = stream_.state_.load(std::memory_order_acquire);
        if (oldState == state::source_next_active_stream_stopped) {
          stream_.cleanupOp_ = this;
          if (stream_.state_.compare_exchange_strong(
                oldState, state::source_next_active_cleanup_requested,
                std::memory_order_release,
                std::memory_order_acquire)) {
            // Successfully signalled that cleanup has been requested and
            // that the next() operation should call start_cleanup() when
            // it completes.
            return;
          }
        }

        // Otherwise, next() operation has completed so we are responsible
        // for starting
        if (oldState == state::source_next_completed) {
          // A prior next() call has been made on the underlying stream and
          // so we need to call cleanup().
          start_cleanup();
          return;
        }

        // No prior next() call has been made. Nothing to do for cleanup.
        // Send done() immediately.
        assert(oldState == state::not_started);
        unifex::set_done(std::move(receiver_));
      }

      void start_cleanup() noexcept final {
        try {
          cleanupOp_.construct_from([&] {
            return unifex::connect(
              cleanup(stream_.source_),
              receiver_wrapper{*this});
          });
          unifex::start(cleanupOp_.get());
        } catch (...) {
          // Prefer to send the error from next(source_) over the error
          // from cleanup(source_) if there was one.
          if (stream_.nextError_) {
            unifex::set_error(std::move(receiver_), std::move(stream_.nextError_));
          } else {
            unifex::set_error(std::move(receiver_), std::current_exception());
          }
        }
      }
    };

    template<typename Receiver>
    auto connect(Receiver&& receiver) && {
      return operation<std::remove_cvref_t<Receiver>>{
        stream_,
        (Receiver&&)receiver};
    }
  };

  UNIFEX_NO_UNIQUE_ADDRESS SourceStream source_;
  std::atomic<state> state_{state::not_started};
  cleanup_operation_base* cleanupOp_ = nullptr;
  next_receiver_base* nextReceiver_ = nullptr;
  inplace_stop_source stopSource_;
  std::exception_ptr nextError_;
  manual_lifetime<next_operation_t<SourceStream, next_receiver>> nextOp_;

public:

  template<typename SourceStream2>
  explicit stop_immediately_stream(SourceStream2&& source)
  : source_((SourceStream2&&)source)
  {}

  stop_immediately_stream(stop_immediately_stream&& other)
  : source_(std::move(other.source_))
  {}

  friend next_sender tag_invoke(tag_t<next>, stop_immediately_stream& s) {
    return {s};
  }

  friend cleanup_sender tag_invoke(tag_t<cleanup>, stop_immediately_stream& s) {
    return {s};
  }
};

template<typename... Values, typename SourceStream>
auto stop_immediately(SourceStream&& source) {
  return stop_immediately_stream<std::remove_cvref_t<SourceStream>, Values...>{
    (SourceStream&&)source};
}

} // namespace unifex
