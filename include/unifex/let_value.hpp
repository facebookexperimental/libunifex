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

#include <unifex/async_trace.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/manual_lifetime_union.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/bind_back.hpp>

#include <exception>
#include <functional>
#include <tuple>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _let_v {
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
    auto& op = op_;
    UNIFEX_TRY {
      // Taking by value here to force a copy on the offchance the value
      // objects lives in the operation state (e.g., just), in which
      // case the call to cleanup() would invalidate them.
      [&](auto... copies) {
        cleanup();
        unifex::set_value(
            std::move(op.receiver_), (decltype(copies) &&) copies...);
      } ((SuccessorValues&&) values...);
    } UNIFEX_CATCH (...) {
      unifex::set_error(std::move(op.receiver_), std::current_exception());
    }
  }

  void set_done() && noexcept {
    auto& op = op_;
    cleanup();
    unifex::set_done(std::move(op.receiver_));
  }

  // Taking by value here to force a copy on the offchance the error
  // object lives in the operation state (e.g., just_error), in which
  // case the call to cleanup() would invalidate it.
  template <typename Error>
  void set_error(Error error) && noexcept {
    auto& op = op_;
    cleanup();
    unifex::set_error(std::move(op.receiver_), (Error &&) error);
  }

private:
  template <typename... Values2>
  using successor_operation = typename Operation::template successor_operation<Values2...>;

  void cleanup() noexcept {
    auto& op = op_;
    unifex::deactivate_union_member<successor_operation<Values...>>(op.succOp_);
    op.values_.template destruct<decayed_tuple<Values...>>();
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const successor_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const typename Operation::receiver_type&>)
      -> callable_result_t<CPO, const typename Operation::receiver_type&> {
    return std::move(cpo)(std::as_const(r.get_receiver()));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const successor_receiver& r,
      Func&& f) {
    std::invoke(f, r.get_receiver());
  }
#endif
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
    auto& op = op_;
    UNIFEX_TRY {
      scope_guard destroyPredOp =
        [&]() noexcept { unifex::deactivate_union_member(op.predOp_); };
      auto& valueTuple =
        op.values_.template construct<decayed_tuple<Values...>>((Values &&) values...);
      destroyPredOp.reset();
      scope_guard destroyValues = [&]() noexcept {
        op.values_.template destruct<decayed_tuple<Values...>>();
      };
      auto& succOp =
          unifex::activate_union_member_with<successor_operation<Values...>>(
            op.succOp_,
            [&] {
              return unifex::connect(
                  std::apply(std::move(op.func_), valueTuple),
                  successor_receiver<Operation, Values...>{op});
            });
      unifex::start(succOp);
      destroyValues.release();
    } UNIFEX_CATCH (...) {
      unifex::set_error(std::move(op.receiver_), std::current_exception());
    }
  }

  void set_done() && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.predOp_);
    unifex::set_done(std::move(op.receiver_));
  }

  // Taking by value here to force a copy on the offchange the error
  // object lives in the operation state, in which case destroying the
  // predecessor operation state would invalidate it.
  template <typename Error>
  void set_error(Error error) && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.predOp_);
    unifex::set_error(std::move(op.receiver_), (Error &&) error);
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const predecessor_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const receiver_type&>)
      -> callable_result_t<CPO, const receiver_type&> {
    return std::move(cpo)(std::as_const(r.get_receiver()));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const predecessor_receiver& r,
      Func&& f) {
    std::invoke(f, r.get_receiver());
  }
#endif
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
    unifex::activate_union_member_with(predOp_, [&] {
      return unifex::connect(
          (Predecessor &&) pred, predecessor_receiver<operation>{*this});
    });
  }

  ~type() {
    if (!started_) {
      unifex::deactivate_union_member(predOp_);
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
  UNIFEX_NO_UNIQUE_ADDRESS typename sender_traits<predecessor_type>::
      template value_types<manual_lifetime_union, decayed_tuple>
          values_;
  union {
    manual_lifetime<connect_result_t<Predecessor, predecessor_receiver<operation>>> predOp_;
    typename sender_traits<predecessor_type>::template
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

template<typename Sender>
struct sends_done_impl : std::bool_constant<sender_traits<Sender>::sends_done> {};

template <typename... Successors>
using any_sends_done = std::disjunction<sends_done_impl<Successors>...>;

template <typename Sender, typename... Rest>
struct max_blocking_kind {
  constexpr auto operator()() noexcept { return cblocking<Sender>(); }
};

template <typename First, typename Second, typename... Rest>
struct max_blocking_kind<First, Second, Rest...> {
  constexpr auto operator()() noexcept {
    constexpr blocking_kind first = cblocking<First>();
    constexpr blocking_kind second = cblocking<Second>();

    if constexpr (first == second) {
      return max_blocking_kind<First, Rest...>{}();
    } else if constexpr (
        first == blocking_kind::always &&
        second == blocking_kind::always_inline) {
      return max_blocking_kind<First, Rest...>{}();
    } else if constexpr (
        first == blocking_kind::always_inline &&
        second == blocking_kind::always) {
      return max_blocking_kind<Second, Rest...>{}();
    } else {
      return blocking_kind::maybe;
    }
  }
};

constexpr blocking_kind _blocking_kind(blocking_kind source, blocking_kind completion) noexcept {
  if (source == blocking_kind::never || completion == blocking_kind::never) {
    return blocking_kind::never;
  } else if (
      source == blocking_kind::always_inline &&
      completion == blocking_kind::always_inline) {
    return blocking_kind::always_inline;
  } else if (
      (source == blocking_kind::always_inline ||
       source == blocking_kind::always) &&
      (completion == blocking_kind::always_inline ||
       completion == blocking_kind::always)) {
    return blocking_kind::always;
  } else {
    return blocking_kind::maybe;
  }
}

template <typename Predecessor, typename SuccessorFactory>
class _sender<Predecessor, SuccessorFactory>::type {
  using sender = type;
  Predecessor pred_;
  SuccessorFactory func_;

  template <typename... Values>
  using successor_type = std::invoke_result_t<SuccessorFactory, std::decay_t<Values>&...>;

  template <template <typename...> class List>
  using successor_types =
      sender_value_types_t<Predecessor, List, successor_type>;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  struct value_types_impl {
    template <typename... Senders>
    using apply =
        typename concat_type_lists_unique_t<
            sender_value_types_t<Senders, type_list, Tuple>...>::template apply<Variant>;
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
    using apply =
        typename concat_type_lists_unique_t<
            sender_error_types_t<Senders, type_list>...,
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

  static constexpr bool sends_done =
    sender_traits<Predecessor>::sends_done ||
    successor_types<any_sends_done>::value;

 public:
  template <typename Predecessor2, typename SuccessorFactory2>
  explicit type(Predecessor2&& pred, SuccessorFactory2&& func)
      noexcept(std::is_nothrow_constructible_v<Predecessor, Predecessor2> &&
          std::is_nothrow_constructible_v<SuccessorFactory, SuccessorFactory2>)
    : pred_((Predecessor2 &&) pred), func_((SuccessorFactory2 &&) func) {}

  template(typename CPO, typename Sender, typename Receiver)
      (requires same_as<CPO, tag_t<unifex::connect>> AND
        same_as<remove_cvref_t<Sender>, type>)
  friend auto tag_invoke([[maybe_unused]] CPO cpo, Sender&& sender, Receiver&& receiver)
      -> operation<decltype((static_cast<Sender&&>(sender).pred_)), SuccessorFactory, Receiver> {
    return operation<decltype((static_cast<Sender&&>(sender).pred_)), SuccessorFactory, Receiver>{
        static_cast<Sender&&>(sender).pred_,
        static_cast<Sender&&>(sender).func_,
        static_cast<Receiver&&>(receiver)};
  }

  friend constexpr auto tag_invoke(tag_t<unifex::blocking>, const type&) noexcept {
    constexpr blocking_kind succ = successor_types<_let_v::max_blocking_kind>{}();
    if constexpr (
        blocking_kind::never == cblocking<Predecessor>() || blocking_kind::never == succ) {
      return blocking_kind::never;
    } else if constexpr (
        blocking_kind::maybe != cblocking<Predecessor>() && blocking_kind::maybe != succ) {
      return blocking_kind::constant<_let_v::_blocking_kind(cblocking<Predecessor>(), succ)>{};
    } else {
      return _let_v::_blocking_kind(cblocking<Predecessor>(), succ);
    }
  }
};

namespace _cpo {
  struct _fn {
    template <typename Predecessor, typename SuccessorFactory>
    auto operator()(Predecessor&& pred, SuccessorFactory&& func) const
        noexcept(std::is_nothrow_constructible_v<
            _let_v::sender<Predecessor, SuccessorFactory>, Predecessor, SuccessorFactory>)
        -> _let_v::sender<Predecessor, SuccessorFactory> {
      return _let_v::sender<Predecessor, SuccessorFactory>{
          (Predecessor &&) pred,
          (SuccessorFactory &&) func};
    }
    template <typename SuccessorFactory>
    constexpr auto operator()(SuccessorFactory&& func) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, SuccessorFactory>)
        -> bind_back_result_t<_fn, SuccessorFactory> {
      return bind_back(*this, (SuccessorFactory&&)func);
    }
  };
} // namespace _cpo
} // namespace _let_v

inline constexpr _let_v::_cpo::_fn let_value {};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
