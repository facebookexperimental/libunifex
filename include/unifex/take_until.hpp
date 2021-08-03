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

#include <unifex/config.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/bind_back.hpp>
#include <unifex/exception.hpp>

#include <exception>
#include <atomic>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _take_until {
template <typename SourceStream, typename TriggerStream>
struct _stream {
  struct type;
};
template <typename SourceStream, typename TriggerStream>
using stream =
    typename _stream<
        remove_cvref_t<SourceStream>,
        remove_cvref_t<TriggerStream>>::type;

template <typename SourceStream, typename TriggerStream>
struct _stream<SourceStream, TriggerStream>::type {
 private:
  using take_until_stream = type;
  struct trigger_next_receiver {
    take_until_stream& stream_;

    template <typename... Values>
    void set_value(Values&&...) && noexcept {
      std::move(*this).set_done();
    }

    template <typename Error>
    void set_error(Error&&) && noexcept {
      std::move(*this).set_done();
    }

    void set_done() && noexcept {
      auto& stream = stream_;
      stream.triggerNextOp_.destruct();
      stream.trigger_next_done();
    }

    inplace_stop_source& get_stop_source() const {
      return stream_.stopSource_;
    }

    friend inplace_stop_token tag_invoke(
        tag_t<get_stop_token>, const trigger_next_receiver& r) noexcept {
      return r.get_stop_source().get_token();
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

    template <template <typename...> class Variant,
             template <typename...> class Tuple>
    using value_types =
      typename sender_traits<next_sender_t<SourceStream>>::
        template value_types<Variant, Tuple>;

    template <template <typename...> class Variant>
    using error_types =
      sender_error_types_t<next_sender_t<SourceStream>, Variant>;

    static constexpr bool sends_done = sender_traits<next_sender_t<SourceStream>>::sends_done;

    template <typename Receiver>
    struct _op {
      struct type {
        struct receiver_wrapper {
          type& op_;

          template <typename... Values>
          void set_value(Values&&... values) && noexcept {
            op_.stopCallback_.destruct();
            unifex::set_value(std::move(op_.receiver_), (Values&&)values...);
          }

          void set_done() && noexcept {
            op_.stopCallback_.destruct();
            op_.stream_.stopSource_.request_stop();
            unifex::set_done(std::move(op_.receiver_));
          }

          template <typename Error>
          void set_error(Error&& error) && noexcept {
            op_.stopCallback_.destruct();
            op_.stream_.stopSource_.request_stop();
            unifex::set_error(std::move(op_.receiver_), (Error&&)error);
          }

          inplace_stop_source& get_stop_source() const {
            return op_.stream_.stopSource_;
          }

          friend inplace_stop_token tag_invoke(
              tag_t<get_stop_token>, const receiver_wrapper& r) noexcept {
            return r.get_stop_source().get_token();
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

        template <typename Receiver2>
        explicit type(take_until_stream& stream, Receiver2&& receiver)
          : stream_(stream)
          , receiver_((Receiver2&&)receiver)
          , innerOp_(unifex::connect(
                next(stream.source_),
                receiver_wrapper{*this}))
        {}

        void start() noexcept {
          if (!stream_.triggerNextStarted_) {
            stream_.triggerNextStarted_ = true;

            UNIFEX_TRY {
              stream_.triggerNextOp_.construct_with([&] {
                return unifex::connect(
                  next(stream_.trigger_),
                  trigger_next_receiver{stream_});
              });
              unifex::start(stream_.triggerNextOp_.get());
            } UNIFEX_CATCH (...) {
              stream_.trigger_next_done();
            }
          }

          stopCallback_.construct(
            get_stop_token(receiver_),
            cancel_callback{stream_.stopSource_});
          unifex::start(innerOp_);
        }
      };
    };
    template <typename Receiver>
    using operation = typename _op<remove_cvref_t<Receiver>>::type;

    template <typename Receiver>
    operation<Receiver> connect(Receiver&& receiver) && {
      return operation<Receiver>{
        stream_,
        (Receiver&&)receiver};
    }
  };

  struct cleanup_sender {
    take_until_stream& stream_;

    template <template <typename...> class Variant,
             template <typename...> class Tuple>
    using value_types =
      typename sender_traits<cleanup_sender_t<SourceStream>>::
        template value_types<Variant, Tuple>;

    template <template <typename...> class Variant>
    using error_types =
      sender_error_types_t<cleanup_sender_t<SourceStream>, Variant>;

    static constexpr bool sends_done = true;

    template <typename Receiver>
    struct _op {
      struct type final : cleanup_operation_base {
        struct source_receiver {
          type& op_;

          void set_done() && noexcept {
            auto& op = op_;
            op.sourceOp_.destruct();
            op.source_cleanup_done();
          }

          template <typename Error>
          void set_error(Error&& error) && noexcept {
            std::move(*this).set_error(make_exception_ptr((Error&&)error));
          }

          void set_error(std::exception_ptr error) && noexcept {
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
          type& op_;

          void set_done() && noexcept {
            auto& op = op_;
            op.sourceOp_.destruct();
            op.trigger_cleanup_done();
          }

          template <typename Error>
          void set_error(Error&& error) && noexcept {
            std::move(*this).set_error(make_exception_ptr((Error&&)error));
          }

          void set_error(std::exception_ptr error) && noexcept {
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

        template <typename Receiver2>
        explicit type(take_until_stream& stream, Receiver2&& receiver)
          : stream_(stream)
          , receiver_((Receiver2&&)receiver)
        {}

        void start() noexcept {
          UNIFEX_TRY {
            sourceOp_.construct_with([&] {
              return unifex::connect(
                cleanup(stream_.source_),
                source_receiver{*this});
            });
            unifex::start(sourceOp_.get());
          } UNIFEX_CATCH (...) {
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
          UNIFEX_TRY {
            triggerOp_.construct_with([&] {
              return unifex::connect(
                cleanup(stream_.trigger_),
                trigger_receiver{*this});
            });
            unifex::start(triggerOp_.get());
          } UNIFEX_CATCH (...) {
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
            unifex::set_error(std::move(receiver_), std::move(triggerError_));
          } else {
            unifex::set_done(std::move(receiver_));
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
          unifex::set_error(std::move(receiver_), std::move(sourceError_));
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
            unifex::set_error(std::move(receiver_), std::move(sourceError_));
          } else {
            unifex::set_done(std::move(receiver_));
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
            unifex::set_error(std::move(receiver_), std::move(sourceError_));
          } else {
            unifex::set_error(std::move(receiver_), std::move(triggerError_));
          }
        }
      };
    };
    template <typename Receiver>
    using operation = typename _op<remove_cvref_t<Receiver>>::type;

    template <typename Receiver>
    operation<Receiver> connect(Receiver&& receiver) {
      return operation<Receiver>{stream_, (Receiver &&) receiver};
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
          // Successfully registered completion of next(trigger)
          // before someone called cleanup(stream). We have passed
          // responsibility for calling cleanup(trigger_) to the
          // call to start() on the cleanup(stream) sender.
          return;
        }
      }

      // Otherwise, the cleanup(stream) operation has already been started
      // before the next(trigger) operation finished.
      // We have the responsibility for launching cleanup(trigger).
      UNIFEX_ASSERT(cleanupOperation_ != nullptr);
      cleanupOperation_->start_trigger_cleanup();
  }

public:

  template <typename SourceStream2, typename TriggerStream2>
  explicit type(SourceStream2&& source, TriggerStream2&& trigger)
  : source_((SourceStream2&&)source)
  , trigger_((TriggerStream2&&)trigger)
  {}

  type(type&& other)
  : source_(std::move(other.source_))
  , trigger_(std::move(other.trigger_))
  {}

  friend next_sender tag_invoke(tag_t<next>, take_until_stream& s) {
    return {s};
  }

  friend cleanup_sender tag_invoke(tag_t<cleanup>, take_until_stream& s) {
    return {s};
  }
};
} // namespace _take_until

namespace _take_until_cpo {
  inline const struct _fn {
    template <typename SourceStream, typename TriggerStream>
    auto operator()(SourceStream&& source, TriggerStream&& trigger) const {
      return _take_until::stream<SourceStream, TriggerStream>{
        (SourceStream&&)source,
        (TriggerStream&&)trigger};
    }
    template <typename TriggerStream>
    constexpr auto operator()(TriggerStream&& trigger) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, TriggerStream>)
        -> bind_back_result_t<_fn, TriggerStream> {
      return bind_back(*this, (TriggerStream&&)trigger);
    }
  } take_until {};
} // namespace _take_until_cpo

using _take_until_cpo::take_until;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
