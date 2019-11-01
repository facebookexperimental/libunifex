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
#include <unifex/sender_concepts.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/get_stop_token.hpp>

#include <exception>
#include <atomic>
#include <type_traits>
#include <utility>
#include <cassert>

namespace unifex {

template<typename SourceStream, typename TriggerStream>
struct take_until_stream {
 private:

  struct trigger_next_receiver {
    take_until_stream& stream_;

    template<typename... Values>
    void value(Values&&...) && noexcept {
      std::move(*this).done();
    }

    template<typename Error>
    void error(Error&&) && noexcept {
      std::move(*this).done();
    }

    void done() && noexcept {
      auto& stream = stream_;
      stream.triggerNextOp_.destruct();
      stream.trigger_next_done();
    }

    friend inplace_stop_token tag_invoke(
        tag_t<get_stop_token>, const trigger_next_receiver& r) noexcept {
      return r.stream_.stopSource_.get_token();
    }
  };

  struct cleanup_operation_base {
    virtual void start_trigger_cleanup() noexcept = 0;
  };

  struct cancel_callback {
    inplace_stop_source& stopSource_;

    void operator()() noexcept {
      stopSource_.request_stop();
    }
  };

  struct next_sender {
    take_until_stream& stream_;

    template<template<typename...> class Variant,
             template<typename...> class Tuple>
    using value_types =
      typename next_sender_t<SourceStream>::
        template value_types<Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types =
      typename next_sender_t<SourceStream>::template error_types<Variant>;

    template<typename Receiver>
    struct operation {
      struct receiver_wrapper {
        operation& op_;

        template<typename... Values>
        void value(Values&&... values) && noexcept {
          op_.stopCallback_.destruct();
          cpo::set_value(std::move(op_.receiver_), (Values&&)values...);
        }

        void done() && noexcept {
          op_.stopCallback_.destruct();
          op_.stream_.stopSource_.request_stop();
          cpo::set_done(std::move(op_.receiver_));
        }

        template<typename Error>
        void error(Error&& error) && noexcept {
          op_.stopCallback_.destruct();
          op_.stream_.stopSource_.request_stop();
          cpo::set_error(std::move(op_.receiver_), (Error&&)error);
        }

        friend inplace_stop_token tag_invoke(
            tag_t<get_stop_token>, const receiver_wrapper& r) noexcept {
          return r.op_.stream_.stopSource_.get_token();
        }

        template <typename Func>
        friend void tag_invoke(
            tag_t<visit_continuations>,
            const receiver_wrapper& r,
            Func&& func) {
          std::invoke(func, r.op_.receiver_);
        }
      };

      take_until_stream& stream_;
      Receiver receiver_;
      manual_lifetime<typename stop_token_type_t<Receiver&>::
                      template callback_type<cancel_callback>>
        stopCallback_;
      next_operation_t<SourceStream, receiver_wrapper> innerOp_;

      template<typename Receiver2>
      explicit operation(take_until_stream& stream, Receiver2&& receiver)
      : stream_(stream)
      , receiver_((Receiver2&&)receiver)
      , innerOp_(cpo::connect(
          cpo::next(stream.source_),
          receiver_wrapper{*this}))
      {}

      void start() noexcept {
        if (!stream_.triggerNextStarted_) {
          stream_.triggerNextStarted_ = true;
          try {
            stream_.triggerNextOp_.construct_from([&] {
              return cpo::connect(
                cpo::next(stream_.trigger_),
                trigger_next_receiver{stream_});
            });
            cpo::start(stream_.triggerNextOp_.get());
          } catch (...) {
            stream_.trigger_next_done();
          }
        }

        stopCallback_.construct(
          get_stop_token(receiver_),
          cancel_callback{stream_.stopSource_});
        cpo::start(innerOp_);
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
    take_until_stream& stream_;

    template<template<typename...> class Variant,
             template<typename...> class Tuple>
    using value_types =
      typename cleanup_sender_t<SourceStream>::
        template value_types<Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types =
      typename cleanup_sender_t<SourceStream>::template error_types<Variant>;

    template<typename Receiver>
    struct operation final : cleanup_operation_base {
      struct source_receiver {
        operation& op_;

        void done() && noexcept {
          auto& op = op_;
          op.sourceOp_.destruct();
          op.source_cleanup_done();
        }

        template<typename Error>
        void error(Error&& error) && noexcept {
          std::move(*this).error(std::make_exception_ptr((Error&&)error));
        }

        void error(std::exception_ptr error) && noexcept {
          auto& op = op_;
          op.sourceOp_.destruct();
          op.source_cleanup_error(std::move(error));
        }

        template <typename Func>
        friend void tag_invoke(
            tag_t<visit_continuations>,
            const source_receiver& r,
            Func&& func) {
          std::invoke(func, r.op_.receiver_);
        }
      };

      struct trigger_receiver {
        operation& op_;

        void done() && noexcept {
          auto& op = op_;
          op.sourceOp_.destruct();
          op.trigger_cleanup_done();
        }

        template<typename Error>
        void error(Error&& error) && noexcept {
          std::move(*this).error(std::make_exception_ptr((Error&&)error));
        }

        void error(std::exception_ptr error) && noexcept {
          auto& op = op_;
          op.triggerOp_.destruct();
          op.trigger_cleanup_error(std::move(error));
        }

        template <typename Func>
        friend void tag_invoke(
            tag_t<visit_continuations>,
            const trigger_receiver& r,
            Func&& func) {
          std::invoke(func, r.op_.receiver_);
        }
      };

      take_until_stream& stream_;
      std::atomic<bool> cleanupCompleted_ = false;
      std::exception_ptr sourceError_;
      std::exception_ptr triggerError_;
      UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

      manual_lifetime<cleanup_operation_t<SourceStream, source_receiver>>
        sourceOp_;
      manual_lifetime<cleanup_operation_t<TriggerStream, trigger_receiver>>
        triggerOp_;

      template<typename Receiver2>
      explicit operation(take_until_stream& stream, Receiver2&& receiver)
      : stream_(stream)
      , receiver_((Receiver2&&)receiver)
      {}

      void start() noexcept {
        try {
          sourceOp_.construct_from([&] {
            return cpo::connect(
              cpo::cleanup(stream_.source_),
              source_receiver{*this});
          });
          cpo::start(sourceOp_.get());
        } catch (...) {
          source_cleanup_error(std::current_exception());
        }

        if (!stream_.cleanupReady_.load(std::memory_order_acquire)) {
          stream_.cleanupOperation_ = this;
          stream_.stopSource_.request_stop();
          if (!stream_.cleanupReady_.exchange(true, std::memory_order_acq_rel)) {
            // The trigger cleanup is not yet ready to run.
            // The trigger_next_receiver will start this when it completes.
            return;
          }
        }

        // Otherwise, the trigger cleanup is ready to start.
        start_trigger_cleanup();
      }

      void start_trigger_cleanup() noexcept final {
        try {
          triggerOp_.construct_from([&] {
            return cpo::connect(
              cpo::cleanup(stream_.trigger_),
              trigger_receiver{*this});
          });
          cpo::start(triggerOp_.get());
        } catch (...) {
          trigger_cleanup_error(std::current_exception());
          return;
        }
      }

      void source_cleanup_done() noexcept {
        if (!cleanupCompleted_.load(std::memory_order_acquire)) {
          if (!cleanupCompleted_.exchange(true, std::memory_order_acq_rel)) {
            // We were first to register completion of the cleanup op.
            // Let the other operation call the final receiver.
            return;
          }
        }

        // The other operation finished first.
        if (triggerError_) {
          cpo::set_error(std::move(receiver_), std::move(triggerError_));
        } else {
          cpo::set_done(std::move(receiver_));
        }
      }

      void source_cleanup_error(std::exception_ptr ex) noexcept {
        sourceError_ = std::move(ex);

        if (!cleanupCompleted_.load(std::memory_order_acquire)) {
          if (!cleanupCompleted_.exchange(true, std::memory_order_acq_rel)) {
            // trigger cleanup not yet finished.
            // let the trigger_receiver call the final receiver.
            return;
          }
        }

        // Trigger cleanup finished first
        // Prefer to propagate the source cleanup error over the trigger
        // cleanup error if there was one.
        cpo::set_error(std::move(receiver_), std::move(sourceError_));
      }

      void trigger_cleanup_done() noexcept {
        if (!cleanupCompleted_.load(std::memory_order_acquire)) {
          if (!cleanupCompleted_.exchange(true, std::memory_order_acq_rel)) {
            // We were first to register completion of the cleanup op.
            // Let the other operation call the final receiver.
            return;
          }
        }

        // The other operation finished first.
        if (sourceError_) {
          cpo::set_error(std::move(receiver_), std::move(sourceError_));
        } else {
          cpo::set_done(std::move(receiver_));
        }
      }

      void trigger_cleanup_error(std::exception_ptr ex) noexcept {
        triggerError_ = std::move(ex);

        if (!cleanupCompleted_.load(std::memory_order_acquire)) {
          if (!cleanupCompleted_.exchange(true, std::memory_order_acq_rel)) {
            // source cleanup not yet finished.
            // let the source_receiver call the final receiver.
            return;
          }
        }

        // Source cleanup finished first
        // Prefer to propagate the source cleanup error over the trigger
        // cleanup error if there was one.
        if (sourceError_) {
          cpo::set_error(std::move(receiver_), std::move(sourceError_));
        } else {
          cpo::set_error(std::move(receiver_), std::move(triggerError_));
        }
      }
    };

    template<typename Receiver>
    operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) {
      return operation<std::remove_cvref_t<Receiver>>{
        stream_,
        (Receiver&&)receiver};
    }
  };

  UNIFEX_NO_UNIQUE_ADDRESS SourceStream source_;
  UNIFEX_NO_UNIQUE_ADDRESS TriggerStream trigger_;
  inplace_stop_source stopSource_;
  cleanup_operation_base* cleanupOperation_ = nullptr;
  std::atomic<bool> cleanupReady_ = false;
  bool triggerNextStarted_ = false;

  manual_lifetime<next_operation_t<TriggerStream, trigger_next_receiver>>
    triggerNextOp_;

  void trigger_next_done() noexcept {
      if (!cleanupReady_.load(std::memory_order_acquire)) {
        stopSource_.request_stop();
        if (!cleanupReady_.exchange(true, std::memory_order_acq_rel)) {
          // Successfully registered completion of trigger.next()
          // before someone called stream.cleanup(). We have passed
          // responsibility for calling trigger_.cleanup() to the
          // stream.cleanup().start() method.
          return;
        }
      }

      // Otherwise, the stream.cleanup() operation has already been started
      // before the trigger.next() operation finished.
      // We have the responsibility for launching trigger.cleanup().
      assert(cleanupOperation_ != nullptr);
      cleanupOperation_->start_trigger_cleanup();
  }

public:

  template<typename SourceStream2, typename TriggerStream2>
  explicit take_until_stream(SourceStream2&& source, TriggerStream2&& trigger)
  : source_((SourceStream2&&)source)
  , trigger_((TriggerStream2&&)trigger)
  {}

  take_until_stream(take_until_stream&& other)
  : source_(std::move(other.source_))
  , trigger_(std::move(other.trigger_))
  {}

  next_sender next() {
    return {*this};
  }

  cleanup_sender cleanup() {
    return {*this};
  }

};

template<typename SourceStream, typename TriggerStream>
auto take_until(SourceStream&& source, TriggerStream&& trigger) {
  return take_until_stream<std::remove_cvref_t<SourceStream>,
                           std::remove_cvref_t<TriggerStream>>{
    (SourceStream&&)source,
    (TriggerStream&&)trigger};
}

} // namespace unifex
