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
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/manual_lifetime_union.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>

#include <exception>
#include <functional>
#include <tuple>
#include <type_traits>

namespace unifex {

template <typename Predecessor, typename SuccessorFactory>
class let_sender {
  Predecessor pred_;
  SuccessorFactory func_;

  template <typename... Values>
  using successor_type =
      std::invoke_result_t<SuccessorFactory, std::decay_t<Values>&...>;

  // TODO: Ideally we'd only conditionally add the std::exception_ptr type
  // to the list of error types if it's possible that one of the following
  // operations is potentially throwing.
  //
  // Need to check whether any of the following bits are potentially-throwing:
  // - the construction of the value copies
  // - the invocation of the successor factory
  // - the invocation of the 'connect()' operation for the receiver.
  //
  // Unfortunately, we can't really check this last point reliably until we
  // know the concrete receiver type. So for now we conseratively report that
  // we might output std::exception_ptr.

  template <template <typename...> class List>
  using successor_types =
      typename Predecessor::template value_types<List, successor_type>;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  struct value_types_impl {
   public:
    template <typename... Senders>
    using apply = typename concat_type_lists_unique_t<
        typename Senders::template value_types<type_list, Tuple>...
        >::template apply<Variant>;
  };

  template <template <typename...> class Variant>
  struct error_types_impl {
    template <typename... Senders>
    using apply = typename concat_type_lists_unique_t<
        typename Senders::template error_types<type_list>...,
        type_list<std::exception_ptr>>::template apply<Variant>;
  };

 public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types =
      successor_types<value_types_impl<Variant, Tuple>::template apply>;

  template <template <typename...> class Variant>
  using error_types =
      successor_types<error_types_impl<Variant>::template apply>;

 private:
  template <typename Predecessor2, typename Receiver>
  class operation {
    template <typename... Values>
    struct successor_receiver;

    template <typename... Values>
    using successor_operation =
        operation_t<successor_type<Values...>, successor_receiver<Values...>>;

    template <typename... Values>
    using decayed_tuple = std::tuple<std::decay_t<Values>...>;

    template <typename... Values>
    struct successor_receiver {
      operation& op_;

      Receiver&  get_receiver() const { return op_.receiver_; }

      template <typename... SuccessorValues>
      void set_value(SuccessorValues&&... values) && noexcept {
        cleanup();
        try {
          unifex::set_value(
              std::move(op_.receiver_), (SuccessorValues &&) values...);
        } catch (...) {
          unifex::set_error(std::move(op_.receiver_), std::current_exception());
        }
      }

      void set_done() && noexcept {
        cleanup();
        unifex::set_done(std::move(op_.receiver_));
      }

      template <typename Error>
      void set_error(Error&& error) && noexcept {
        cleanup();
        unifex::set_error(std::move(op_.receiver_), (Error &&) error);
      }

     private:
      void cleanup() noexcept {
        op_.succOp_.template get<successor_operation<Values...>>().destruct();
        op_.values_.template get<decayed_tuple<Values...>>().destruct();
      }

      template <
          typename CPO,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const successor_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.get_receiver()));
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const successor_receiver& r,
          Func&& f) {
        std::invoke(f, r.get_receiver());
      }
    };

    struct predecessor_receiver {
      operation& op_;

      Receiver&  get_receiver() const { return op_.receiver_; }

      template <typename... Values>
      void set_value(Values&&... values) && noexcept {
        bool destroyedPredOp = false;
        try {
          auto& valueTuple =
              op_.values_.template get<decayed_tuple<Values...>>();
          valueTuple.construct((Values &&) values...);
          destroyedPredOp = true;
          op_.predOp_.destruct();
          try {
            auto& succOp =
                op_.succOp_.template get<successor_operation<Values...>>()
                    .construct_from([&] {
                      return unifex::connect(
                          std::apply(std::move(op_.func_), valueTuple.get()),
                          successor_receiver<Values...>{op_});
                    });
            unifex::start(succOp);
          } catch (...) {
            valueTuple.destruct();
            throw;
          }
        } catch (...) {
          if (!destroyedPredOp) {
            op_.predOp_.destruct();
          }
          unifex::set_error(std::move(op_.receiver_), std::current_exception());
        }
      }

      void set_done() && noexcept {
        op_.predOp_.destruct();
        unifex::set_done(std::move(op_.receiver_));
      }

      template <typename Error>
      void set_error(Error&& error) && noexcept {
        op_.predOp_.destruct();
        unifex::set_error(std::move(op_.receiver_), (Error &&) error);
      }

      template <
          typename CPO,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const predecessor_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.get_receiver()));
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const predecessor_receiver& r,
          Func&& f) {
        std::invoke(f, r.get_receiver());
      }
    };

   public:
    template <typename SuccessorFactory2, typename Receiver2>
    explicit operation(
        Predecessor2&& pred,
        SuccessorFactory2&& func,
        Receiver2&& receiver)
        : func_((SuccessorFactory2 &&) func),
          receiver_((Receiver2 &&) receiver) {
      predOp_.construct_from([&] {
        return unifex::connect((Predecessor2 &&) pred, predecessor_receiver{*this});
      });
    }

    ~operation() {
      if (!started_) {
        predOp_.destruct();
      }
    }

    void start() noexcept {
      started_ = true;
      unifex::start(predOp_.get());
    }

   private:
    UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    UNIFEX_NO_UNIQUE_ADDRESS typename Predecessor::
        template value_types<manual_lifetime_union, decayed_tuple>
            values_;
    union {
      manual_lifetime<operation_t<Predecessor2, predecessor_receiver>> predOp_;
      typename Predecessor::
          template value_types<manual_lifetime_union, successor_operation>
              succOp_;
    };
    bool started_ = false;
  };

 public:
  template <typename Predecessor2, typename SuccessorFactory2>
  explicit let_sender(Predecessor2&& pred, SuccessorFactory2&& func)
      : pred_((Predecessor2 &&) pred), func_((SuccessorFactory2 &&) func) {}

  template <typename Receiver>
  operation<Predecessor, std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
    return operation<Predecessor, std::remove_cvref_t<Receiver>>{
        std::move(pred_), std::move(func_), (Receiver &&) receiver};
  }

  template <typename Receiver>
  operation<Predecessor&, std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) & {
    return operation<Predecessor&, std::remove_cvref_t<Receiver>>{
        pred_, func_, (Receiver &&) receiver};
  }

  template <typename Receiver>
  operation<const Predecessor&, std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) const & {
    return operation<const Predecessor&, std::remove_cvref_t<Receiver>>{
      pred_, func_, (Receiver &&) receiver};
  }
};

template <typename Predecessor, typename SuccessorFactory>
auto let(Predecessor&& pred, SuccessorFactory&& func) {
  return let_sender<
      std::remove_cvref_t<Predecessor>,
      std::remove_cvref_t<SuccessorFactory>>{(Predecessor &&) pred,
                                             (SuccessorFactory &&) func};
}

} // namespace unifex
