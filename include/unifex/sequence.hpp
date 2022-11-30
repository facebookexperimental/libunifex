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
#include <unifex/async_trace.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/std_concepts.hpp>

#include <exception>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  namespace _seq
  {
    constexpr blocking_kind _blocking_kind(
        blocking_kind predBlocking, blocking_kind succBlocking) noexcept {
      if (predBlocking == blocking_kind::never) {
        return blocking_kind::never;
      } else if (
          predBlocking == blocking_kind::always_inline &&
          succBlocking == blocking_kind::always_inline) {
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

    template <typename Predecessor, typename Successor, typename Receiver>
    struct _op {
      class type;
    };
    template <typename Predecessor, typename Successor, typename Receiver>
    using operation = typename _op<
        Predecessor,
        Successor,
        remove_cvref_t<Receiver>>::type;

    template <typename Predecessor, typename Successor, typename Receiver>
    struct _successor_receiver {
      class type;
    };
    template <typename Predecessor, typename Successor, typename Receiver>
    using successor_receiver =
        typename _successor_receiver<
            Predecessor,
            Successor,
            remove_cvref_t<Receiver>>::type;

    template <typename Predecessor, typename Successor, typename Receiver>
    class _successor_receiver<Predecessor, Successor, Receiver>::type final {
      using successor_receiver = type;
      using operation_type = operation<Predecessor, Successor, Receiver>;

    public:
      explicit type(operation_type* op) noexcept
        : op_(op) {}

      type(type&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

    private:
      template(typename CPO, typename R, typename... Args)
          (requires
              is_receiver_cpo_v<CPO> AND
              same_as<R, successor_receiver> AND
              is_callable_v<CPO, Receiver, Args...>)
      friend auto tag_invoke(
          CPO cpo,
          R&& r,
          Args&&... args) noexcept(is_nothrow_callable_v<
                                           CPO,
                                           Receiver,
                                           Args...>)
          -> callable_result_t<CPO, Receiver, Args...> {
        return static_cast<CPO&&>(cpo)(
            r.get_receiver_rvalue(), static_cast<Args&&>(args)...);
      }

      template(typename CPO, typename R)
          (requires is_receiver_query_cpo_v<CPO> AND
            same_as<R, successor_receiver> AND
            is_callable_v<CPO, const Receiver&>)
      friend auto tag_invoke(
          CPO cpo,
          const R& r) noexcept(is_nothrow_callable_v<
                                           CPO,
                                           const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
        return static_cast<CPO&&>(cpo)(r.get_const_receiver());
      }

    #if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const successor_receiver& r,
          Func&& func) {
        std::invoke(func, r.get_const_receiver());
      }
    #endif

      Receiver&& get_receiver_rvalue() noexcept {
        return static_cast<Receiver&&>(op_->receiver_);
      }

      const Receiver& get_const_receiver() const noexcept {
        return op_->receiver_;
      }

      operation_type* op_;
    };

    template <typename Predecessor, typename Successor, typename Receiver>
    struct _predecessor_receiver {
      class type;
    };
    template <typename Predecessor, typename Successor, typename Receiver>
    using predecessor_receiver =
        typename _predecessor_receiver<
            Predecessor,
            Successor,
            remove_cvref_t<Receiver>>::type;

    template <typename Predecessor, typename Successor, typename Receiver>
    class _predecessor_receiver<Predecessor, Successor, Receiver>::type final {
      using predecessor_receiver = type;
      using operation_type = operation<Predecessor, Successor, Receiver>;

    public:
      explicit type(operation_type* op) noexcept
        : op_(op) {}

      type(type&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

      void set_value() && noexcept {
        // Take a copy of op_ before destroying predOp_ as this may end up
        // destroying *this.
        using successor_receiver_t =
            successor_receiver<Predecessor, Successor, Receiver>;

        auto* op = op_;
        op->status_ = operation_type::status::empty;
        unifex::deactivate_union_member(op->predOp_);
        if constexpr (is_nothrow_connectable_v<Successor, successor_receiver_t>) {
          unifex::activate_union_member_with(op->succOp_, [&]() noexcept {
            return unifex::connect(
                static_cast<Successor&&>(op->successor_), successor_receiver_t{op});
          });
          op->status_ = operation_type::status::successor_operation_constructed;
          unifex::start(op->succOp_.get());
        } else {
          UNIFEX_TRY {
            unifex::activate_union_member_with(op->succOp_, [&] {
              return unifex::connect(
                  static_cast<Successor&&>(op->successor_), successor_receiver_t{op});
            });
            op->status_ = operation_type::status::successor_operation_constructed;
            unifex::start(op->succOp_.get());
          } UNIFEX_CATCH (...) {
            unifex::set_error(
                static_cast<Receiver&&>(op->receiver_),
                std::current_exception());
          }
        }
      }

      template(typename Error)
          (requires receiver<Receiver, Error>)
      void set_error(Error&& error) && noexcept {
        unifex::set_error(
            static_cast<Receiver&&>(op_->receiver_),
            static_cast<Error&&>(error));
      }

      void set_done() && noexcept {
        unifex::set_done(static_cast<Receiver&&>(op_->receiver_));
      }

    private:
      template(typename CPO, typename R)
          (requires is_receiver_query_cpo_v<CPO> AND
            same_as<R, predecessor_receiver> AND
            is_callable_v<CPO, const Receiver&>)
      friend auto tag_invoke(
          CPO cpo,
          const R& r) noexcept(is_nothrow_callable_v<
                                           CPO,
                                           const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
        return static_cast<CPO&&>(cpo)(r.get_const_receiver());
      }

    #if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const predecessor_receiver& r,
          Func&& func) {
        std::invoke(func, r.get_const_receiver());
      }
    #endif

      const Receiver& get_const_receiver() const noexcept {
        return op_->receiver_;
      }

      operation_type* op_;
    };

    template <typename Predecessor, typename Successor, typename Receiver>
    class _op<Predecessor, Successor, Receiver>::type {
      using operation = type;
    public:
      template <typename Successor2, typename Receiver2>
      explicit type(
          Predecessor&& predecessor,
          Successor2&& successor,
          Receiver2&& receiver)
        : successor_(static_cast<Successor2&&>(successor))
        , receiver_(static_cast<Receiver&&>(receiver))
        , status_(status::predecessor_operation_constructed) {
        unifex::activate_union_member_with(predOp_, [&] {
          return unifex::connect(
              static_cast<Predecessor&&>(predecessor),
              predecessor_receiver<Predecessor, Successor, Receiver>{this});
        });
      }

      ~type() {
        switch (status_) {
          case status::predecessor_operation_constructed:
            unifex::deactivate_union_member(predOp_);
            break;
          case status::successor_operation_constructed:
            unifex::deactivate_union_member(succOp_);
            break;
          case status::empty: break;
        }
      }

      void start() & noexcept {
        UNIFEX_ASSERT(status_ == status::predecessor_operation_constructed);
        unifex::start(predOp_.get());
      }

    private:
      friend predecessor_receiver<
          Predecessor,
          Successor,
          Receiver>;
      friend successor_receiver<
          Predecessor,
          Successor,
          Receiver>;

      Successor successor_;
      Receiver receiver_;
      enum class status {
        empty,
        predecessor_operation_constructed,
        successor_operation_constructed
      };
      status status_;
      union {
        manual_lifetime<connect_result_t<
            Predecessor,
            predecessor_receiver<Predecessor, Successor, Receiver>>>
            predOp_;
        manual_lifetime<connect_result_t<
            Successor,
            successor_receiver<Predecessor, Successor, Receiver>>>
            succOp_;
      };
    };

    template <typename Predecessor, typename Successor>
    struct _sndr {
      struct type;
    };
    template <typename Predecessor, typename Successor>
    using _sender = typename _sndr<
        remove_cvref_t<Predecessor>,
        remove_cvref_t<Successor>>::type;

    template <typename Predecessor, typename Successor>
    struct _sndr<Predecessor, Successor>::type {
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types =
          sender_value_types_t<Successor, Variant, Tuple>;

      template <template <typename...> class Variant>
      using error_types =
          typename concat_type_lists_unique_t<
              sender_error_types_t<Predecessor, type_list>,
              sender_error_types_t<Successor, type_list>,
              type_list<std::exception_ptr>>::template apply<Variant>;

      static constexpr bool sends_done =
        sender_traits<Predecessor>::sends_done ||
        sender_traits<Successor>::sends_done;

      template(typename Predecessor2, typename Successor2)
          (requires constructible_from<Predecessor, Predecessor2> AND
              constructible_from<Successor, Successor2>)
      explicit type(Predecessor2&& predecessor, Successor2&& successor)
          noexcept(std::is_nothrow_constructible_v<Predecessor, Predecessor2> &&
              std::is_nothrow_constructible_v<Successor, Successor2>)
        : predecessor_(static_cast<Predecessor&&>(predecessor))
        , successor_(static_cast<Successor&&>(successor)) {}

      friend auto tag_invoke(tag_t<blocking>, const type& self) {
        if constexpr (
            blocking_kind::maybe != cblocking<Predecessor>() &&
            blocking_kind::maybe != cblocking<Successor>()) {
          return blocking_kind::constant<
              _seq::_blocking_kind(cblocking<Predecessor>(), cblocking<Successor>())>();
        } else {
          return _seq::_blocking_kind(
              blocking(self.predecessor_),
              blocking(self.successor_));
        }
      }

      template(typename Receiver, typename Sender)
          (requires same_as<remove_cvref_t<Sender>, type> AND
            constructible_from<Successor, member_t<Sender, Successor>> AND
            sender_to<
              member_t<Sender, Predecessor>,
              predecessor_receiver<member_t<Sender, Predecessor>, Successor, Receiver>> AND
            sender_to<
              Successor,
              successor_receiver<member_t<Sender, Predecessor>, Successor, Receiver>>)
      friend auto tag_invoke(tag_t<unifex::connect>, Sender&& sender, Receiver&& receiver)
          -> operation<member_t<Sender, Predecessor>, Successor, Receiver> {
        return operation<member_t<Sender, Predecessor>, Successor,  Receiver>{
            static_cast<Sender&&>(sender).predecessor_,
            static_cast<Sender&&>(sender).successor_,
            (Receiver &&) receiver};
      }

    private:
      UNIFEX_NO_UNIQUE_ADDRESS Predecessor predecessor_;
      UNIFEX_NO_UNIQUE_ADDRESS Successor successor_;
    };

    namespace _cpo {
      struct _fn {
        // Sequencing a single sender is just the same as returning the sender
        // itself.
        template <typename First>
        remove_cvref_t<First> operator()(First&& first) const
            noexcept(std::is_nothrow_constructible_v<remove_cvref_t<First>, First>) {
          return static_cast<First&&>(first);
        }

        template(typename First, typename Second)
          (requires sender<First> AND sender<Second> AND //
            tag_invocable<_fn, First, Second>)
        auto operator()(First&& first, Second&& second) const
            noexcept(is_nothrow_tag_invocable_v<_fn, First, Second>)
            -> tag_invoke_result_t<_fn, First, Second> {
          return unifex::tag_invoke(
              _fn{}, static_cast<First&&>(first), static_cast<Second&&>(second));
        }

        template(typename First, typename Second)
          (requires sender<First> AND sender<Second> AND //
            (!tag_invocable<_fn, First, Second>))
        auto operator()(First&& first, Second&& second) const
            noexcept(std::is_nothrow_constructible_v<
                    _seq::_sender<First, Second>,
                    First,
                    Second>)
                -> _seq::_sender<First, Second> {
          return _seq::_sender<First, Second>{
              static_cast<First&&>(first),
              static_cast<Second&&>(second)};
        }

        template(typename First, typename Second, typename Third, typename... Rest)
          (requires sender<First> AND sender<Second> AND sender<Third> AND
            (sender<Rest> &&...) AND tag_invocable<_fn, First, Second, Third, Rest...>)
        auto operator()(First&& first, Second&& second, Third&& third, Rest&&... rest) const
            noexcept(is_nothrow_tag_invocable_v<_fn, First, Second, Third, Rest...>)
            -> tag_invoke_result_t<_fn, First, Second, Third, Rest...> {
          return unifex::tag_invoke(
              _fn{},
              static_cast<First&&>(first),
              static_cast<Second&&>(second),
              static_cast<Third&&>(third),
              static_cast<Rest&&>(rest)...);
        }

        template(typename First, typename Second, typename Third, typename... Rest)
          (requires sender<First> AND sender<Second> AND sender<Third> AND
            (sender<Rest> &&...) AND (!tag_invocable<_fn, First, Second, Third, Rest...>))
        auto operator()(First&& first, Second&& second, Third&& third, Rest&&... rest) const
            noexcept(is_nothrow_callable_v<_fn, First, Second> &&
                is_nothrow_callable_v<
                    _fn,
                    callable_result_t<_fn, First, Second>,
                    Third,
                    Rest...>)
            -> callable_result_t<
                _fn,
                callable_result_t<_fn, First, Second>,
                Third,
                Rest...> {
          // Fall-back to pair-wise invocation of the sequence() CPO.
          return (*this)(
              (*this)(static_cast<First&&>(first), static_cast<Second&&>(second)),
              static_cast<Third&&>(third),
              static_cast<Rest&&>(rest)...);
        }
      };
    } // _cpo
  } // namespace _seq

  inline constexpr _seq::_cpo::_fn sequence {};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
