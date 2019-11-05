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

#include <unifex/async_trace.hpp>
#include <unifex/config.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/get_stop_token.hpp>

#include <unifex/get_stop_token.hpp>

namespace unifex {

template <typename... Values>
struct type_erased_stream {
  struct next_receiver_base {
    virtual void value(Values&&... values) noexcept = 0;
    virtual void done() noexcept = 0;
    virtual void error(std::exception_ptr ex) noexcept = 0;

    using visitor_t = void(const continuation_info&, void*);

   private:
    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const next_receiver_base& receiver,
        Func&& func) {
      visit_continuations(get_continuation_info(), (Func &&) func);
    }

    virtual continuation_info get_continuation_info() const = 0;
  };

  struct cleanup_receiver_base {
    virtual void done() noexcept = 0;
    virtual void error(std::exception_ptr ex) noexcept = 0;

   private:
    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const next_receiver_base& receiver,
        Func&& func) {
      visit_continuations(get_continuation_info(), (Func &&) func);
    }

    virtual continuation_info get_continuation_info() const noexcept = 0;
  };

  struct stream_base {
    virtual ~stream_base() {}
    virtual void start_next(
        next_receiver_base& receiver,
        inplace_stop_token stopToken) noexcept = 0;
    virtual void start_cleanup(cleanup_receiver_base& receiver) noexcept = 0;
  };

  template <typename Receiver>
  struct concrete_next_receiver : next_receiver_base {
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    explicit concrete_next_receiver(Receiver&& receiver)
        : receiver_((Receiver &&) receiver) {}

    void value(Values&&... values) noexcept override {
      cpo::set_value(std::move(receiver_), (Values &&) values...);
    }

    void done() noexcept override {
      cpo::set_done(std::move(receiver_));
    }

    void error(std::exception_ptr ex) noexcept override {
      cpo::set_error(std::move(receiver_), std::move(ex));
    }

   private:
    continuation_info get_continuation_info() const noexcept {
      return continuation_info::from_continuation(receiver_);
    }
  };

  template <typename Receiver>
  struct concrete_cleanup_receiver final : cleanup_receiver_base {
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    explicit concrete_cleanup_receiver(Receiver&& receiver)
        : receiver_((Receiver &&) receiver) {}

    void done() noexcept override {
      cpo::set_done(std::move(receiver_));
    }

    void error(std::exception_ptr ex) noexcept override {
      cpo::set_error(std::move(receiver_), std::move(ex));
    }

   private:
    continuation_info get_continuation_info() const noexcept {
      return continuation_info::from_continuation(receiver_);
    }
  };

  template <typename Stream>
  struct concrete_stream final : stream_base {
    UNIFEX_NO_UNIQUE_ADDRESS Stream stream_;

    // TODO: static_assert that all values() overloads produced
    // by source Stream are convertible to and same arity as Values...

    struct next_receiver_wrapper {
      next_receiver_base& receiver_;
      concrete_stream& stream_;
      inplace_stop_token stopToken_;

      void value(Values&&... values) && noexcept {
        try {
          // Take a copy of the values before destroying the next operation
          // state in case the values are references to objects stored in
          // the operation object.
          [&](Values... values) {
            stream_.next_.destruct();
            receiver_.value((Values &&) values...);
          }((Values &&) values...);
        } catch (...) {
          stream_.next_.destruct();
          receiver_.error(std::current_exception());
        }
      }

      void done() && noexcept {
        stream_.next_.destruct();
        receiver_.done();
      }

      void error(std::exception_ptr ex) && noexcept {
        stream_.next_.destruct();
        receiver_.error(std::move(ex));
      }

      template <typename Error>
      void error(Error&& error) && noexcept {
        // Type-erase any errors that come through.
        std::move(*this).error(std::make_exception_ptr((Error&&)error));
      }

      friend const inplace_stop_token& tag_invoke(
          tag_t<get_stop_token>, const next_receiver_wrapper& r) noexcept {
        return r.stopToken_;
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const next_receiver_wrapper& receiver,
          Func&& func) {
        visit_continuations(receiver.receiver_, (Func &&) func);
      }
    };

    struct cleanup_receiver_wrapper {
      cleanup_receiver_base& receiver_;
      concrete_stream& stream_;

      void done() && noexcept {
        stream_.cleanup_.destruct();
        receiver_.done();
      }

      void error(std::exception_ptr ex) && noexcept {
        stream_.cleanup_.destruct();
        receiver_.error(std::move(ex));
      }

      template <typename Error>
      void error(Error&& error) && noexcept {
        // Type-erase any errors that come through.
        std::move(*this).error(std::make_exception_ptr((Error&)error));
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const cleanup_receiver_wrapper& receiver,
          Func&& func) {
        visit_continuations(receiver.receiver_, (Func &&) func);
      }
    };

    template <typename Stream2>
    explicit concrete_stream(Stream2&& stream) : stream_((Stream2 &&) stream) {}

    ~concrete_stream() {}

    union {
      manual_lifetime<next_operation_t<Stream, next_receiver_wrapper>>
          next_;
      manual_lifetime<cleanup_operation_t<Stream, cleanup_receiver_wrapper>>
          cleanup_;
    };

    void start_next(
        next_receiver_base& receiver,
        inplace_stop_token stopToken) noexcept override {
      try {
        next_
            .construct_from([&] {
              return cpo::connect(
                  cpo::next(stream_),
                  next_receiver_wrapper{receiver, *this, std::move(stopToken)});
            });
        cpo::start(next_.get());
      } catch (...) {
        receiver.error(std::current_exception());
      }
    }

    void start_cleanup(cleanup_receiver_base& receiver) noexcept override {
      try {
        cleanup_
            .construct_from([&] {
              return cpo::connect(
                  cpo::cleanup(stream_),
                  cleanup_receiver_wrapper{receiver, *this});
            });
        cpo::start(cleanup_.get());
      } catch (...) {
        receiver.error(std::current_exception());
      }
    }
  };

  struct next_sender {
    stream_base& stream_;

    template<template<typename...> class Variant,
             template<typename...> class Tuple>
    using value_types = Variant<Tuple<Values...>>;

    template<template<typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    template <typename Receiver>
    struct operation {
      struct cancel_callback {
        inplace_stop_source& stopSource_;
        void operator()() noexcept {
          stopSource_.request_stop();
        }
      };

      stream_base& stream_;
      inplace_stop_source stopSource_;
      concrete_next_receiver<Receiver> receiver_;
      UNIFEX_NO_UNIQUE_ADDRESS
          typename stop_token_type_t<Receiver&>::
          template callback_type<cancel_callback>
        stopCallback_;

      template <typename Receiver2>
      explicit operation(stream_base& stream, Receiver2&& receiver)
          : stream_(stream),
            stopSource_(),
            receiver_((Receiver2 &&) receiver),
            stopCallback_(
              get_stop_callback(receiver_.receiver_),
              cancel_callback{stopSource_})
          {}

      void start() noexcept {
        stream_.start_next(
          receiver_,
          get_stop_token(receiver_.receiver_).stop_possible() ?
          stopSource_.get_token() : inplace_stop_token{});
      }
    };

    template <typename Receiver>
    operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) {
      return operation<std::remove_cvref_t<Receiver>>{
          stream_, (Receiver &&) receiver};
    }
  };

  struct cleanup_sender {
    stream_base& stream_;

    template<template<typename...> class Variant,
             template<typename...> class Tuple>
    using value_types = Variant<>;

    template<template<typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    template <typename Receiver>
    struct operation {
      stream_base& stream_;
      concrete_cleanup_receiver<Receiver> receiver_;

      explicit operation(stream_base& stream, Receiver&& receiver)
          : stream_(stream), receiver_((Receiver &&) receiver) {}

      void start() noexcept {
        stream_.start_cleanup(receiver_);
      }
    };

    template <typename Receiver>
    operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) {
      return operation<std::remove_cvref_t<Receiver>>{stream_,
                                                      (Receiver &&) receiver};
    }
  };

  std::unique_ptr<stream_base> stream_;

  template <typename ConcreteStream>
  explicit type_erased_stream(ConcreteStream&& stream)
      : stream_(std::make_unique<
                concrete_stream<std::remove_cvref_t<ConcreteStream>>>(
            (ConcreteStream &&) stream)) {}

  next_sender next() noexcept {
    return next_sender{*stream_};
  }

  cleanup_sender cleanup() noexcept {
    return cleanup_sender{*stream_};
  }
};

template <typename... Ts, typename Stream>
type_erased_stream<Ts...> type_erase(Stream&& stream) {
  return type_erased_stream<Ts...>{(Stream &&) stream};
}

} // namespace unifex
