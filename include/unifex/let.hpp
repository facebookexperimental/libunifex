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
#include <unifex/std_concepts.hpp>

#include <exception>
#include <functional>
#include <tuple>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _let {
template <typename... Values>
using decayed_tuple = std::tuple<std::decay_t<Values>...>;

template <typename Operation, typename... Values>
struct _successor_receiver {
  struct type;
};
template <typename Operation, typename... Values>
using successor_receiver = typename _successor_receiver<Operation, Values...>::type;

template <typename Operation, typename... Values>
struct _successor_receiver<Operation, Values...>::type {
  using successor_receiver = type;
  Operation& op_;

  typename Operation::receiver_type& get_receiver() const noexcept {
    return op_.receiver_;
  }

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
  template <typename... Values2>
  using successor_operation = typename Operation::template successor_operation<Values2...>;

  void cleanup() noexcept {
    op_.succOp_.template get<successor_operation<Values...>>().destruct();
    op_.values_.template get<decayed_tuple<Values...>>().destruct();
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const successor_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const typename Operation::receiver_type&>)
      -> callable_result_t<CPO, const typename Operation::receiver_type&> {
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

template <typename Operation>
struct _predecessor_receiver {
  struct type;
};
template <typename Operation>
using predecessor_receiver = typename _predecessor_receiver<Operation>::type;

template <typename Operation>
struct _predecessor_receiver<Operation>::type {
  using predecessor_receiver = type;
  using receiver_type = typename Operation::receiver_type;

  template <typename... Values>
  using successor_operation = typename Operation::template successor_operation<Values...>;

  Operation& op_;

  receiver_type& get_receiver() const noexcept {
    return op_.receiver_;
  }

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
                      successor_receiver<Operation, Values...>{op_});
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

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const predecessor_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const receiver_type&>)
      -> callable_result_t<CPO, const receiver_type&> {
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

template <typename Predecessor, typename SuccessorFactory, typename Receiver>
struct _op {
  struct type;
};
template <typename Predecessor, typename SuccessorFactory, typename Receiver>
using operation = typename _op<
    Predecessor,
    SuccessorFactory,
    remove_cvref_t<Receiver>>::type;

template <typename Predecessor, typename SuccessorFactory, typename Receiver>
struct _op<Predecessor, SuccessorFactory, Receiver>::type {
  using operation = type;
  using receiver_type = Receiver;

  template <typename... Values>
  using successor_type =
      std::invoke_result_t<SuccessorFactory, std::decay_t<Values>&...>;

  template <typename... Values>
  using successor_operation =
      connect_result_t<successor_type<Values...>, successor_receiver<operation, Values...>>;

  friend predecessor_receiver<operation>;
  template <typename Operation, typename... Values>
  friend struct _successor_receiver;

  template <typename SuccessorFactory2, typename Receiver2>
  explicit type(
      Predecessor&& pred,
      SuccessorFactory2&& func,
      Receiver2&& receiver)
      : func_((SuccessorFactory2 &&) func),
        receiver_((Receiver2 &&) receiver) {
    predOp_.construct_from([&] {
      return unifex::connect((Predecessor &&) pred, predecessor_receiver<operation>{*this});
    });
  }

  ~type() {
    if (!started_) {
      predOp_.destruct();
    }
  }

  void start() noexcept {
    started_ = true;
    unifex::start(predOp_.get());
  }

private:
  using predecessor_type = remove_cvref_t<Predecessor>;
  UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  UNIFEX_NO_UNIQUE_ADDRESS typename predecessor_type::
      template value_types<manual_lifetime_union, decayed_tuple>
          values_;
  union {
    manual_lifetime<connect_result_t<Predecessor, predecessor_receiver<operation>>> predOp_;
    typename predecessor_type::template
        value_types<manual_lifetime_union, successor_operation>
            succOp_;
  };
  bool started_ = false;
};

template <typename Predecessor, typename SuccessorFactory>
struct _sender {
  class type;
};
template <typename Predecessor, typename SuccessorFactory>
using sender = typename _sender<
    remove_cvref_t<Predecessor>,
    remove_cvref_t<SuccessorFactory>>::type;

template <typename Predecessor, typename SuccessorFactory>
class _sender<Predecessor, SuccessorFactory>::type {
  using sender = type;
  Predecessor pred_;
  SuccessorFactory func_;

  template <typename... Values>
  using successor_type = std::invoke_result_t<SuccessorFactory, std::decay_t<Values>&...>;

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

 public:
  template <typename Predecessor2, typename SuccessorFactory2>
  explicit type(Predecessor2&& pred, SuccessorFactory2&& func)
      noexcept(std::is_nothrow_constructible_v<Predecessor, Predecessor2> &&
          std::is_nothrow_constructible_v<SuccessorFactory, SuccessorFactory2>)
    : pred_((Predecessor2 &&) pred), func_((SuccessorFactory2 &&) func) {}

  template(typename CPO, typename Sender, typename Receiver)
      (requires same_as<CPO, tag_t<unifex::connect>> AND
        same_as<remove_cvref_t<Sender>, type>)
  friend auto tag_invoke(CPO cpo, Sender&& sender, Receiver&& receiver)
      -> operation<decltype((static_cast<Sender&&>(sender).pred_)), SuccessorFactory, Receiver> {
    return operation<decltype((static_cast<Sender&&>(sender).pred_)), SuccessorFactory, Receiver>{
        static_cast<Sender&&>(sender).pred_,
        static_cast<Sender&&>(sender).func_,
        static_cast<Receiver&&>(receiver)};
  }
};

} // namespace _let

namespace _let_cpo {
  inline const struct _fn {
    template <typename Predecessor, typename SuccessorFactory>
    auto operator()(Predecessor&& pred, SuccessorFactory&& func) const
        noexcept(std::is_nothrow_constructible_v<
            _let::sender<Predecessor, SuccessorFactory>, Predecessor, SuccessorFactory>)
        -> _let::sender<Predecessor, SuccessorFactory> {
      return _let::sender<Predecessor, SuccessorFactory>{
          (Predecessor &&) pred,
          (SuccessorFactory &&) func};
    }
  } let {};
} // namespace _let_cpo

using _let_cpo::let;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
