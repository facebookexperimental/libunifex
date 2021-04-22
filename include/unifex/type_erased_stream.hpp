/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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
#include <unifex/bind_back.hpp>
#include <unifex/exception.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _type_erase {

template <typename... Values>
struct _stream {
  struct type;
};
template <typename... Values>
using stream = typename _stream<Values...>::type;

template <typename... Values>
struct _stream<Values...>::type {
  struct next_receiver_base {
    virtual void set_value(Values&&... values) noexcept = 0;
    virtual void set_done() noexcept = 0;
    virtual void set_error(std::exception_ptr ex) noexcept = 0;

    using visitor_t = void(const continuation_info&, void*);

   private:
    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const next_receiver_base& receiver,
        Func&& func) {
      visit_continuations(receiver.get_continuation_info(), (Func &&) func);
    }

    virtual continuation_info get_continuation_info() const = 0;
  };

  struct cleanup_receiver_base {
    virtual void set_done() noexcept = 0;
    virtual void set_error(std::exception_ptr ex) noexcept = 0;

   private:
    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const cleanup_receiver_base& receiver,
        Func&& func) {
      visit_continuations(receiver.get_continuation_info(), (Func &&) func);
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
  struct _next_receiver {
    struct type final : next_receiver_base {
      UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

      explicit type(Receiver&& receiver)
        : receiver_((Receiver &&) receiver) {}

      void set_value(Values&&... values) noexcept override {
        unifex::set_value(std::move(receiver_), (Values &&) values...);
      }

      void set_done() noexcept override {
        unifex::set_done(std::move(receiver_));
      }

      void set_error(std::exception_ptr ex) noexcept override {
        unifex::set_error(std::move(receiver_), std::move(ex));
      }

    private:
      continuation_info get_continuation_info() const noexcept override {
        return continuation_info::from_continuation(receiver_);
      }
    };
  };
  template <typename Receiver>
  using next_receiver = typename _next_receiver<remove_cvref_t<Receiver>>::type;

  template <typename Receiver>
  struct _cleanup_receiver {
    struct type final : cleanup_receiver_base {
      UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

      explicit type(Receiver&& receiver)
        : receiver_((Receiver &&) receiver) {}

      void set_done() noexcept override {
        unifex::set_done(std::move(receiver_));
      }

      void set_error(std::exception_ptr ex) noexcept override {
        unifex::set_error(std::move(receiver_), std::move(ex));
      }

    private:
      continuation_info get_continuation_info() const noexcept override {
        return continuation_info::from_continuation(receiver_);
      }
    };
  };
  template <typename Receiver>
  using cleanup_receiver =
      typename _cleanup_receiver<remove_cvref_t<Receiver>>::type;

  template <typename Stream>
  struct _stream {
    struct type final : stream_base {
      using stream = type;
      UNIFEX_NO_UNIQUE_ADDRESS Stream stream_;

      // TODO: static_assert that all values() overloads produced
      // by source Stream are convertible to and same arity as Values...

      struct next_receiver_wrapper {
        next_receiver_base& receiver_;
        stream& stream_;
        inplace_stop_token stopToken_;

        void set_value(Values&&... values) && noexcept {
          UNIFEX_TRY {
            // Take a copy of the values before destroying the next operation
            // state in case the values are references to objects stored in
            // the operation object.
            [&](Values... values) {
              unifex::deactivate_union_member(stream_.next_);
              receiver_.set_value((Values &&) values...);
            }((Values &&) values...);
          } UNIFEX_CATCH (...) {
            unifex::deactivate_union_member(stream_.next_);
            receiver_.set_error(std::current_exception());
          }
        }

        void set_done() && noexcept {
          unifex::deactivate_union_member(stream_.next_);
          receiver_.set_done();
        }

        void set_error(std::exception_ptr ex) && noexcept {
          unifex::deactivate_union_member(stream_.next_);
          receiver_.set_error(std::move(ex));
        }

        template <typename Error>
        void set_error(Error&& error) && noexcept {
          // Type-erase any errors that come through.
          std::move(*this).set_error(make_exception_ptr((Error&&)error));
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
        stream& stream_;

        void set_done() && noexcept {
          unifex::deactivate_union_member(stream_.cleanup_);
          receiver_.set_done();
        }

        void set_error(std::exception_ptr ex) && noexcept {
          unifex::deactivate_union_member(stream_.cleanup_);
          receiver_.set_error(std::move(ex));
        }

        template <typename Error>
        void set_error(Error&& error) && noexcept {
          // Type-erase any errors that come through.
          std::move(*this).set_error(make_exception_ptr((Error&)error));
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
      explicit type(Stream2&& strm) : stream_((Stream2 &&) strm) {}

      ~type() {}

      union {
        manual_lifetime<next_operation_t<Stream, next_receiver_wrapper>>
            next_;
        manual_lifetime<cleanup_operation_t<Stream, cleanup_receiver_wrapper>>
            cleanup_;
      };

      void start_next(
          next_receiver_base& receiver,
          inplace_stop_token stopToken) noexcept override {
        UNIFEX_TRY {
          unifex::activate_union_member_with(next_, [&] {
              return connect(
                  next(stream_),
                  next_receiver_wrapper{receiver, *this, std::move(stopToken)});
            });
          start(next_.get());
        } UNIFEX_CATCH (...) {
          receiver.set_error(std::current_exception());
        }
      }

      void start_cleanup(cleanup_receiver_base& receiver) noexcept override {
        UNIFEX_TRY {
          unifex::activate_union_member_with(cleanup_, [&] {
              return connect(
                  cleanup(stream_),
                  cleanup_receiver_wrapper{receiver, *this});
            });
          start(cleanup_.get());
        } UNIFEX_CATCH (...) {
          receiver.set_error(std::current_exception());
        }
      }
    };
  };
  template <typename Stream>
  using stream = typename _stream<remove_cvref_t<Stream>>::type;

  struct next_sender {
    stream_base& stream_;

    template <template <typename...> class Variant,
             template <typename...> class Tuple>
    using value_types = Variant<Tuple<Values...>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template <typename Receiver>
    struct _op {
      struct type {
        struct cancel_callback {
          inplace_stop_source& stopSource_;
          void operator()() noexcept {
            stopSource_.request_stop();
          }
        };

        stream_base& stream_;
        inplace_stop_source stopSource_;
        next_receiver<Receiver> receiver_;
        UNIFEX_NO_UNIQUE_ADDRESS
            typename stop_token_type_t<Receiver&>::
            template callback_type<cancel_callback>
          stopCallback_;

        template <typename Receiver2>
        explicit type(stream_base& strm, Receiver2&& receiver)
          : stream_(strm),
            stopSource_(),
            receiver_((Receiver2 &&) receiver),
            stopCallback_(
              get_stop_token(receiver_.receiver_),
              cancel_callback{stopSource_})
        {}

        void start() noexcept {
          stream_.start_next(
            receiver_,
            get_stop_token(receiver_.receiver_).stop_possible() ?
            stopSource_.get_token() : inplace_stop_token{});
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

  struct cleanup_sender {
    stream_base& stream_;

    template <template <typename...> class Variant,
             template <typename...> class Tuple>
    using value_types = Variant<>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template <typename Receiver>
    struct _op {
      struct type {
        stream_base& stream_;
        cleanup_receiver<Receiver> receiver_;

        explicit type(stream_base& stream, Receiver&& receiver)
          : stream_(stream)
          , receiver_((Receiver &&) receiver) {}

        void start() noexcept {
          stream_.start_cleanup(receiver_);
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

  std::unique_ptr<stream_base> stream_;

  template <typename ConcreteStream>
  explicit type(ConcreteStream&& strm)
    : stream_(
        std::make_unique<type::stream<ConcreteStream>>(
            (ConcreteStream &&) strm)) {}

  friend next_sender tag_invoke(tag_t<next>, type& s) noexcept {
    return next_sender{*s.stream_};
  }

  friend cleanup_sender tag_invoke(tag_t<cleanup>, type& s) noexcept {
    return cleanup_sender{*s.stream_};
  }
};
} // namespace _type_erase

namespace _type_erase_cpo {
  template <typename... Ts>
  struct _fn {
    template <typename Stream>
    _type_erase::stream<Ts...> operator()(Stream&& strm) const {
      return _type_erase::stream<Ts...>{(Stream &&) strm};
    }
    constexpr auto operator()() const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn>)
        -> bind_back_result_t<_fn> {
      return bind_back(*this);
    }
  };
} // namespace _type_erase_cpo

template <typename... Ts>
inline constexpr _type_erase_cpo::_fn<Ts...> type_erase {};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
