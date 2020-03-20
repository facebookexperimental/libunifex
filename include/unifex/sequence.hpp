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
  namespace _seq
  {
    template <typename Predecessor, typename Successor, typename Receiver>
    struct _op {
      class type;
    };
    template <typename Predecessor, typename Successor, typename Receiver>
    using operation = typename _op<
        Predecessor,
        Successor,
        std::remove_cvref_t<Receiver>>::type;

    template <typename Predecessor, typename Successor, typename Receiver>
    struct _successor_receiver {
      class type;
    };
    template <typename Predecessor, typename Successor, typename Receiver>
    using successor_receiver =
        typename _successor_receiver<
            Predecessor,
            Successor,
            std::remove_cvref_t<Receiver>>::type;

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
      template <
          typename CPO,
          typename R,
          typename... Args,
          std::enable_if_t<
            std::conjunction_v<
              is_receiver_cpo<CPO>,
              std::is_same<R, successor_receiver>,
              is_callable<CPO, Receiver, Args...>>, int> = 0>
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

      template <
          typename CPO,
          typename R,
          typename... Args,
          std::enable_if_t<
            std::conjunction_v<
              std::negation<is_receiver_cpo<CPO>>,
              std::is_same<R, successor_receiver>,
              is_callable<CPO, const Receiver&, Args...>>, int> = 0>
      friend auto tag_invoke(
          CPO cpo,
          const R& r,
          Args&&... args) noexcept(is_nothrow_callable_v<
                                           CPO,
                                           const Receiver&,
              Args...>)
          -> callable_result_t<CPO, const Receiver&, Args...> {
        return static_cast<CPO&&>(cpo)(
            r.get_const_receiver(), static_cast<Args&&>(args)...);
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const successor_receiver& r,
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
    struct _predecessor_receiver {
      class type;
    };
    template <typename Predecessor, typename Successor, typename Receiver>
    using predecessor_receiver =
        typename _predecessor_receiver<
            Predecessor,
            Successor,
            std::remove_cvref_t<Receiver>>::type;

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
        op->predOp_.destruct();
        if constexpr (is_nothrow_connectable_v<Successor, successor_receiver_t>) {
          op->succOp_.construct_from([&]() noexcept {
            return unifex::connect(
                static_cast<Successor&&>(op->successor_), successor_receiver_t{op});
          });
          op->status_ = operation_type::status::successor_operation_constructed;
          unifex::start(op->succOp_.get());
        } else {
          try {
            op->succOp_.construct_from([&]() {
              return unifex::connect(
                  static_cast<Successor&&>(op->successor_), successor_receiver_t{op});
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
              is_callable_v<decltype(unifex::set_error), Receiver, Error>,
              int> = 0>
      void set_error(Error&& error) && noexcept {
        unifex::set_error(
            static_cast<Receiver&&>(op_->receiver_),
            static_cast<Error&&>(error));
      }

      template <
          typename... Args,
          std::enable_if_t<
              is_callable_v<decltype(unifex::set_done), Receiver, Args...>,
              int> = 0>
      void set_done(Args...) && noexcept {
        unifex::set_done(static_cast<Receiver&&>(op_->receiver_));
      }

    private:
      template <
          typename CPO,
          typename R,
          typename... Args,
          std::enable_if_t<
            std::conjunction_v<
              std::negation<is_receiver_cpo<CPO>>,
              std::is_same<R, predecessor_receiver>,
              is_callable<CPO, const Receiver&, Args...>>, int> = 0>
      friend auto tag_invoke(
          CPO cpo,
          const R& r,
          Args&&... args) noexcept(is_nothrow_callable_v<
                                           CPO,
                                           const Receiver&,
                                           Args...>)
          -> callable_result_t<CPO, const Receiver&, Args...> {
        return static_cast<CPO&&>(cpo)(
            r.get_const_receiver(), static_cast<Args&&>(args)...);
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const predecessor_receiver& r,
          Func&& func) {
        std::invoke(func, r.get_const_receiver());
      }

      const Receiver& get_const_receiver() const noexcept {
        return op_->receiver_;
      }

      operation_type* op_;
    };

    template <typename Predecessor, typename Successor, typename Receiver>
    class _op<Predecessor, Successor, Receiver>::type {
      using operation = type;
    public:
      template <typename Predecessor2, typename Successor2, typename Receiver2>
      explicit type(
          Predecessor2&& predecessor,
          Successor2&& successor,
          Receiver2&& receiver)
        : successor_(static_cast<Successor2&&>(successor))
        , receiver_(static_cast<Receiver&&>(receiver))
        , status_(status::predecessor_operation_constructed) {
        predOp_.construct_from([&] {
          return unifex::connect(
              static_cast<Predecessor2&&>(predecessor),
              predecessor_receiver<Predecessor, Successor, Receiver>{this});
        });
      }

      ~type() {
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
      friend predecessor_receiver<
          Predecessor,
          Successor,
          Receiver>;
      friend successor_receiver<
          Predecessor,
          Successor,
          Receiver>;

      std::remove_cvref_t<Successor> successor_;
      Receiver receiver_;
      enum class status {
        empty,
        predecessor_operation_constructed,
        successor_operation_constructed
      };
      status status_;
      union {
        manual_lifetime<operation_t<
            std::remove_cvref_t<Predecessor>,
            predecessor_receiver<Predecessor, Successor, Receiver>>>
            predOp_;
        manual_lifetime<operation_t<
            std::remove_cvref_t<Successor>,
            successor_receiver<Predecessor, Successor, Receiver>>>
            succOp_;
      };
    };

    template <typename Predecessor, typename Successor>
    struct _sender {
      class type;
    };
    template <typename Predecessor, typename Successor>
    using sender = typename _sender<
        std::remove_cvref_t<Predecessor>,
        std::remove_cvref_t<Successor>>::type;

    template <typename Predecessor, typename Successor>
    class _sender<Predecessor, Successor>::type {
      using sender = type;
    public:
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
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
      explicit type(Predecessor2&& predecessor, Successor2&& successor)
          noexcept(std::is_nothrow_constructible_v<Predecessor, Predecessor2> &&
              std::is_nothrow_constructible_v<Successor, Successor2>)
        : predecessor_(static_cast<Predecessor&&>(predecessor))
        , successor_(static_cast<Successor&&>(successor)) {}

      friend blocking_kind
      tag_invoke(tag_t<blocking>, const sender& sender) {
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
            std::conjunction_v<
              is_connectable<
                  Predecessor,
                  predecessor_receiver<Predecessor, Successor, Receiver>>,
              is_connectable<
                  Successor,
                  successor_receiver<Predecessor, Successor, Receiver>>,
              std::is_move_constructible<Successor>>,
              int> = 0>
      auto connect(Receiver&& receiver) &&
          -> operation<Predecessor, Successor,  Receiver> {
        return operation<Predecessor, Successor,  Receiver>{
            (Predecessor &&) predecessor_,
            (Successor &&) successor_,
            (Receiver &&) receiver};
      }

      template <
          typename Receiver,
          std::enable_if_t<
             std::conjunction_v<
               is_connectable<
                  Predecessor&,
                  predecessor_receiver<Predecessor&, Successor, Receiver>>,
               is_connectable<
                  Successor,
                  successor_receiver<Predecessor&, Successor, Receiver>>,
               std::is_copy_constructible<Successor>>,
              int> = 0>
      auto connect(Receiver&& receiver) &
          -> operation<Predecessor&, Successor, Receiver> {
        return operation<Predecessor&, Successor, Receiver>{
            predecessor_, successor_, (Receiver &&) receiver};
      }

      template <
          typename Receiver,
          std::enable_if_t<
              std::conjunction_v<
                is_connectable<
                  const Predecessor&,
                  predecessor_receiver<const Predecessor&, Successor, Receiver>>,
                is_connectable<
                  Successor,
                  successor_receiver<const Predecessor&, Successor, Receiver>>,
                std::is_copy_constructible<Successor>>,
              int> = 0>
      auto connect(Receiver&& receiver) const&
          -> operation<const Predecessor&, Successor, Receiver> {
        return operation<const Predecessor&, Successor, Receiver>{
            predecessor_, successor_, (Receiver &&) receiver};
      }

    private:
      UNIFEX_NO_UNIQUE_ADDRESS Predecessor predecessor_;
      UNIFEX_NO_UNIQUE_ADDRESS Successor successor_;
    };
  }  // namespace _seq

  namespace _seq_cpo {
    inline constexpr struct _fn {
    private:
      template<bool>
      struct _impl2 {
        template <typename First, typename Second>
        auto operator()(First&& first, Second&& second) const
            noexcept(is_nothrow_tag_invocable_v<_fn, First, Second>)
            -> tag_invoke_result_t<_fn, First, Second> {
          return unifex::tag_invoke(
              _fn{}, static_cast<First&&>(first), static_cast<Second&&>(second));
        }
      };

      template<bool>
      struct _impl3 {
        template <typename First, typename Second, typename... Rest>
        auto operator()(First&& first, Second&& second, Rest&&... rest) const
            noexcept(is_nothrow_tag_invocable_v<_fn, First, Second, Rest...>)
            -> tag_invoke_result_t<_fn, First, Second, Rest...> {
          return unifex::tag_invoke(
              _fn{},
              static_cast<First&&>(first),
              static_cast<Second&&>(second),
              static_cast<Rest&&>(rest)...);
        }
      };

    public:
      // Sequencing a single sender is just the same as returning the sender
      // itself.
      template <typename First>
      std::remove_cvref_t<First> operator()(First&& first) const
          noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<First>, First>) {
        return static_cast<First&&>(first);
      }

      template <typename First, typename Second>
      auto operator()(First&& first, Second&& second) const
          noexcept(is_nothrow_callable_v<
              _impl2<is_tag_invocable_v<_fn, First, Second>>, First, Second>)
          -> callable_result_t<
              _impl2<is_tag_invocable_v<_fn, First, Second>>, First, Second> {
        return _impl2<is_tag_invocable_v<_fn, First, Second>>{}(
            static_cast<First&&>(first),
            static_cast<Second&&>(second));
      }

      template <typename First, typename Second, typename Third, typename... Rest>
      auto operator()(First&& first, Second&& second, Third&& third, Rest&&... rest) const
          noexcept(is_nothrow_callable_v<
              _impl3<is_tag_invocable_v<_fn, First, Second, Third, Rest...>>, First, Second, Third, Rest...>)
          -> callable_result_t<
              _impl3<is_tag_invocable_v<_fn, First, Second, Third, Rest...>>, First, Second, Third, Rest...> {
        return _impl3<is_tag_invocable_v<_fn, First, Second, Third, Rest...>>{}(
            static_cast<First&&>(first),
            static_cast<Second&&>(second),
            static_cast<Third&&>(third),
            static_cast<Rest&&>(rest)...);
      }
    } sequence{};

    template<>
    struct _fn::_impl2<false> {
      template <typename First, typename Second>
      auto operator()(First&& first, Second&& second) const
          noexcept(std::is_nothrow_constructible_v<
                  _seq::sender<First, Second>,
                  First,
                  Second>)
              -> _seq::sender<First, Second> {
        return _seq::sender<First, Second>{
            static_cast<First&&>(first),
            static_cast<Second&&>(second)};
      }
    };

    template<>
    struct _fn::_impl3<false> {
      template <typename First, typename Second, typename... Rest>
      auto operator()(First&& first, Second&& second, Rest&&... rest) const
          noexcept(is_nothrow_callable_v<_fn, First, Second> &&
              is_nothrow_callable_v<
                  _fn,
                  callable_result_t<_fn, First, Second>,
                  Rest...>)
          -> callable_result_t<
              _fn,
              callable_result_t<_fn, First, Second>,
              Rest...> {
        // Fall-back to pair-wise invocation of the sequence() CPO.
        return sequence(
            sequence(static_cast<First&&>(first), static_cast<Second&&>(second)),
            static_cast<Rest&&>(rest)...);
      }
    };
  } // _seq_cpo

  using _seq_cpo::sequence;

}  // namespace unifex
