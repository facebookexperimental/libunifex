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
#include <unifex/async_trace.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>

#include <cassert>
#include <exception>
#include <type_traits>
#include <utility>

namespace unifex
{
  namespace detail
  {
    template <typename Predecessor, typename Successor, typename Receiver>
    class sequence_operation;

    template <typename Predecessor, typename Successor, typename Receiver>
    class sequence_successor_receiver {
      using operation_type =
          sequence_operation<Predecessor, Successor, Receiver>;

    public:
      explicit sequence_successor_receiver(operation_type* op) noexcept
        : op_(op) {}

      sequence_successor_receiver(sequence_successor_receiver&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

    private:
      template <typename CPO, typename... Args>
      friend auto tag_invoke(
          CPO cpo,
          sequence_successor_receiver&& r,
          Args&&... args) noexcept(std::
                                       is_nothrow_invocable_v<
                                           CPO,
                                           Receiver,
                                           Args...>)
          -> std::invoke_result_t<CPO, Receiver, Args...> {
        return static_cast<CPO&&>(cpo)(
            r.get_receiver_rvalue(), static_cast<Args&&>(args)...);
      }

      template <typename CPO, typename... Args>
      friend auto tag_invoke(
          CPO cpo,
          const sequence_successor_receiver& r,
          Args&&... args) noexcept(std::
                                       is_nothrow_invocable_v<
                                           CPO,
                                           const Receiver&,
                                           Args...>)
          -> std::invoke_result_t<CPO, const Receiver&, Args...> {
        return static_cast<CPO&&>(cpo)(
            r.get_const_receiver(), static_cast<Args&&>(args)...);
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const sequence_successor_receiver& r,
          Func&& func) {
        std::invoke(func, r.get_const_receiver());
      }

      Receiver&& get_receiver_rvalue() noexcept {
        return static_cast<Receiver&&>(op_->receiver_);
      }

      const Receiver& get_const_receiver() const noexcept {
        return op_->receiver_;
      }

      operation_type* op_;
    };

    template <typename Predecessor, typename Successor, typename Receiver>
    class sequence_predecessor_receiver {
      using operation_type =
          sequence_operation<Predecessor, Successor, Receiver>;

    public:
      explicit sequence_predecessor_receiver(operation_type* op) noexcept
        : op_(op) {}

      sequence_predecessor_receiver(
          sequence_predecessor_receiver&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

      void set_value() && noexcept {
        // Take a copy of op_ before destroying predOp_ as this may end up
        // destroying *this.
        using successor_receiver =
            sequence_successor_receiver<Predecessor, Successor, Receiver>;

        auto* op = op_;
        op->status_ = operation_type::status::empty;
        op->predOp_.destruct();
        if constexpr (is_nothrow_connectable_v<Successor, successor_receiver>) {
          op->succOp_.construct_from([&]() noexcept {
            return unifex::connect(
                static_cast<Successor&&>(op->successor_), successor_receiver{op});
          });
          op->status_ = operation_type::status::successor_operation_constructed;
          unifex::start(op->succOp_.get());
        } else {
          try {
            op->succOp_.construct_from([&]() {
              return unifex::connect(
                  static_cast<Successor&&>(op->successor_), successor_receiver{op});
            });
            op->status_ = operation_type::status::successor_operation_constructed;
            unifex::start(op->succOp_.get());
          } catch (...) {
            unifex::set_error(
                static_cast<Receiver&&>(op->receiver_),
                std::current_exception());
          }
        }
      }

      template <
          typename Error,
          std::enable_if_t<
              std::is_invocable_v<decltype(unifex::set_error), Receiver, Error>,
              int> = 0>
      void set_error(Error&& error) && noexcept {
        unifex::set_error(
            static_cast<Receiver&&>(op_->receiver_),
            static_cast<Error&&>(error));
      }

      template <
          typename... Args,
          std::enable_if_t<
              std::
                  is_invocable_v<decltype(unifex::set_done), Receiver, Args...>,
              int> = 0>
      void set_done(Args...) && noexcept {
        unifex::set_done(static_cast<Receiver&&>(op_->receiver_));
      }

    private:
      template <
          typename CPO,
          typename... Args,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(
          CPO cpo,
          const sequence_predecessor_receiver& r,
          Args&&... args) noexcept(std::
                                       is_nothrow_invocable_v<
                                           CPO,
                                           const Receiver&,
                                           Args...>)
          -> std::invoke_result_t<CPO, const Receiver&, Args...> {
        return static_cast<CPO&&>(cpo)(
            r.get_const_receiver(), static_cast<Args&&>(args)...);
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const sequence_predecessor_receiver& r,
          Func&& func) {
        std::invoke(func, r.get_const_receiver());
      }

      const Receiver& get_const_receiver() const noexcept {
        return op_->receiver_;
      }

      operation_type* op_;
    };

    template <typename Predecessor, typename Successor, typename Receiver>
    class sequence_operation {
    public:
      template <typename Successor2, typename Receiver2>
      explicit sequence_operation(
          Predecessor&& predecessor,
          Successor2&& successor,
          Receiver2&& receiver)
        : successor_(static_cast<Successor2&&>(successor))
        , receiver_(static_cast<Receiver&&>(receiver))
        , status_(status::predecessor_operation_constructed) {
        predOp_.construct_from([&] {
          return unifex::connect(
              static_cast<Predecessor&&>(predecessor),
              sequence_predecessor_receiver<Predecessor, Successor, Receiver>{
                  this});
        });
      }

      ~sequence_operation() {
        switch (status_) {
          case status::predecessor_operation_constructed:
            predOp_.destruct();
            break;
          case status::successor_operation_constructed:
            succOp_.destruct();
            break;
          case status::empty: break;
        }
      }

      void start() & noexcept {
        assert(status_ == status::predecessor_operation_constructed);
        unifex::start(predOp_.get());
      }

    private:
      friend class sequence_predecessor_receiver<
          Predecessor,
          Successor,
          Receiver>;
      friend class sequence_successor_receiver<
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
        manual_lifetime<operation_t<
            Predecessor,
            sequence_predecessor_receiver<Predecessor, Successor, Receiver>>>
            predOp_;
        manual_lifetime<operation_t<
            Successor,
            sequence_successor_receiver<Predecessor, Successor, Receiver>>>
            succOp_;
      };
    };
  }  // namespace detail

  template <typename Predecessor, typename Successor>
  class sequence_sender {
  public:
    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types =
        typename Successor::template value_types<Variant, Tuple>;

    template <template <typename...> class Variant>
    using error_types = typename concat_type_lists_unique_t<
        typename Predecessor::template error_types<type_list>,
        typename Successor::template error_types<type_list>,
        type_list<std::exception_ptr>>::template apply<Variant>;

    template <
        typename Predecessor2,
        typename Successor2,
        std::enable_if_t<
            std::is_constructible_v<Predecessor, Predecessor2> &&
                std::is_constructible_v<Successor, Successor2>,
            int> = 0>
    explicit sequence_sender(
        Predecessor2&& predecessor,
        Successor2&&
            successor) noexcept(std::
                                    is_nothrow_constructible_v<
                                        Predecessor,
                                        Predecessor2>&&
                                        std::is_nothrow_constructible_v<
                                            Successor,
                                            Successor2>)
      : predecessor_(static_cast<Predecessor&&>(predecessor))
      , successor_(static_cast<Successor&&>(successor)) {}

    friend blocking_kind
    tag_invoke(tag_t<blocking>, const sequence_sender& sender) {
      const auto predBlocking = blocking(sender.predecessor_);
      const auto succBlocking = blocking(sender.successor_);
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

    template <
        typename Receiver,
        std::enable_if_t<
            is_connectable_v<
                Predecessor,
                detail::sequence_predecessor_receiver<
                    Predecessor,
                    Successor,
                    std::remove_cvref_t<Receiver>>> &&
                is_connectable_v<
                    Successor,
                    detail::sequence_successor_receiver<
                        Predecessor,
                        Successor,
                        Receiver>>,
            int> = 0>
    auto connect(Receiver&& receiver) && -> detail::sequence_operation<
        Predecessor,
        Successor,
        std::remove_cvref_t<Receiver>> {
      return detail::sequence_operation<
          Predecessor,
          Successor,
          std::remove_cvref_t<Receiver>>{
          (Predecessor &&) predecessor_,
          (Successor &&) successor_,
          (Receiver &&) receiver};
    }

    template <
        typename Receiver,
        std::enable_if_t<
            is_connectable_v<
                Predecessor&,
                detail::sequence_predecessor_receiver<
                    Predecessor&,
                    Successor,
                    std::remove_cvref_t<Receiver>>> &&
                is_connectable_v<
                    Successor,
                    detail::sequence_successor_receiver<
                        Predecessor&,
                        Successor,
                        std::remove_cvref_t<Receiver>>> &&
                std::is_constructible_v<Successor, Successor&>,
            int> = 0>
    auto connect(Receiver&& receiver) & -> detail::sequence_operation<
        Predecessor&,
        Successor,
        std::remove_cvref_t<Receiver>> {
      return detail::sequence_operation<
          Predecessor&,
          Successor,
          std::remove_cvref_t<Receiver>>{
          predecessor_, successor_, (Receiver &&) receiver};
    }

    template <
        typename Receiver,
        std::enable_if_t<
            is_connectable_v<
                const Predecessor&,
                detail::sequence_predecessor_receiver<
                    Predecessor,
                    Successor,
                    Receiver>> &&
                is_connectable_v<
                    Successor,
                    detail::sequence_successor_receiver<
                        Predecessor,
                        Successor,
                        Receiver>> &&
                std::is_constructible_v<Successor, const Successor&>,
            int> = 0>
    auto connect(Receiver&& receiver) const& -> detail::sequence_operation<
        const Predecessor&,
        Successor,
        std::remove_cvref_t<Receiver>> {
      return detail::sequence_operation<
          const Predecessor&,
          Successor,
          std::remove_cvref_t<Receiver>>{
          predecessor_, successor_, (Receiver &&) receiver};
    }

  private:
    UNIFEX_NO_UNIQUE_ADDRESS Predecessor predecessor_;
    UNIFEX_NO_UNIQUE_ADDRESS Successor successor_;
  };

  inline constexpr struct sequence_cpo {
    // Sequencing a single sender is just the same as returning the sender
    // itself.
    template <typename First>
    std::decay_t<First> operator()(First&& first) const
        noexcept(std::is_nothrow_move_constructible_v<First>) {
      return static_cast<First&&>(first);
    }

    template <
        typename First,
        typename Second,
        std::enable_if_t<is_tag_invocable_v<sequence_cpo, First, Second>, int> =
            0>
    auto operator()(First&& first, Second&& second) const
        noexcept(is_nothrow_tag_invocable_v<sequence_cpo, First, Second>)
            -> tag_invoke_result_t<sequence_cpo, First, Second> {
      return unifex::tag_invoke(
          *this, static_cast<First&&>(first), static_cast<Second&&>(second));
    }

    template <
        typename First,
        typename Second,
        std::enable_if_t<
            !is_tag_invocable_v<sequence_cpo, First, Second>,
            int> = 0>
    auto operator()(First&& first, Second&& second) const
        noexcept(std::is_nothrow_constructible_v<
                 sequence_sender<
                     std::remove_cvref_t<First>,
                     std::remove_cvref_t<Second>>,
                 First,
                 Second>)
            -> sequence_sender<
                std::remove_cvref_t<First>,
                std::remove_cvref_t<Second>> {
      return sequence_sender<
          std::remove_cvref_t<First>,
          std::remove_cvref_t<Second>>{
          static_cast<First&&>(first), static_cast<Second&&>(second)};
    }

    template <
        typename First,
        typename Second,
        typename... Rest,
        std::enable_if_t<
            is_tag_invocable_v<sequence_cpo, First, Second, Rest...>,
            int> = 0>
    auto operator()(First&& first, Second&& second, Rest&&... rest) const
        noexcept(
            is_nothrow_tag_invocable_v<sequence_cpo, First, Second, Rest...>)
            -> tag_invoke_result_t<sequence_cpo, First, Second, Rest...> {
      return unifex::tag_invoke(
          *this,
          static_cast<First&&>(first),
          static_cast<Second&&>(second),
          static_cast<Rest&&>(rest)...);
    }

    template <
        typename First,
        typename Second,
        typename... Rest,
        std::enable_if_t<
            !is_tag_invocable_v<sequence_cpo, First, Second, Rest...>,
            int> = 0>
    auto operator()(First&& first, Second&& second, Rest&&... rest) const
        noexcept(is_nothrow_tag_invocable_v<First, Second, Rest...>)
            -> std::invoke_result_t<
                sequence_cpo,
                std::invoke_result_t<sequence_cpo, First, Second>,
                Rest...> {
      // Fall-back to pair-wise invocation of the sequence() CPO.
      return operator()(
          operator()(
              static_cast<First&&>(first), static_cast<Second&&>(second)),
          static_cast<Rest&&>(rest)...);
    }
  } sequence;
}  // namespace unifex
