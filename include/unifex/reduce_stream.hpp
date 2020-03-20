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
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace unifex {
namespace _reduce {
template<typename Operation>
struct _error_cleanup_receiver {
  struct type;
};
template<typename Operation>
using error_cleanup_receiver = typename _error_cleanup_receiver<Operation>::type;

template<typename Operation>
struct _error_cleanup_receiver<Operation>::type {
  using error_cleanup_receiver = type;
  using receiver_type = typename Operation::receiver_type;
  Operation& op_;
  std::exception_ptr ex_;

  // No value() in cleanup receiver

  template <typename Error>
  void set_error(Error error) noexcept {
    auto& op = op_;
    op.errorCleanup_.destruct();
    unifex::set_error(static_cast<receiver_type&&>(op.receiver_), (Error &&) error);
  }

  void set_done() noexcept {
    auto& op = op_;
    auto ex = std::move(ex_);
    op.errorCleanup_.destruct();
    unifex::set_error(static_cast<receiver_type&&>(op.receiver_), std::move(ex));
  }

  template <
      typename CPO,
      std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
  friend auto tag_invoke(CPO cpo, const error_cleanup_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const receiver_type&>)
      -> callable_result_t<CPO, const receiver_type&> {
    return std::move(cpo)(std::as_const(r.op_.receiver_));
  }

  friend unstoppable_token tag_invoke(
      tag_t<get_stop_token>,
      const error_cleanup_receiver&) noexcept {
    return {};
  }

  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const error_cleanup_receiver& r,
      Func&& func) {
    std::invoke(func, r.op_.receiver_);
  }
};

template<typename Operation>
struct _done_cleanup_receiver {
  struct type;
};
template<typename Operation>
using done_cleanup_receiver = typename _done_cleanup_receiver<Operation>::type;

template<typename Operation>
struct _done_cleanup_receiver<Operation>::type {
  using done_cleanup_receiver = type;
  using state_type = typename Operation::state_type;
  using receiver_type = typename Operation::receiver_type;
  Operation& op_;

  template <typename Error>
  void set_error(Error error) && noexcept {
    auto& op = op_;
    op.doneCleanup_.destruct();
    unifex::set_error(static_cast<receiver_type&&>(op.receiver_), (Error &&) error);
  }

  void set_done() && noexcept {
    auto& op = op_;
    op.doneCleanup_.destruct();
    unifex::set_value(
        static_cast<receiver_type&&>(op.receiver_),
        std::forward<state_type>(op.state_));
  }

  template <
      typename CPO,
      std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
  friend auto tag_invoke(CPO cpo, const done_cleanup_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const receiver_type&>)
      -> callable_result_t<CPO, const receiver_type&> {
    return std::move(cpo)(std::as_const(r.op_.receiver_));
  }

  friend unstoppable_token tag_invoke(
      tag_t<get_stop_token>,
      const done_cleanup_receiver&) noexcept {
    return {};
  }

  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const done_cleanup_receiver& r,
      Func&& func) {
    std::invoke(func, r.op_.receiver_);
  }
};

template<typename Operation>
struct _next_receiver {
  struct type;
};
template<typename Operation>
using next_receiver = typename _next_receiver<Operation>::type;

template<typename Operation>
struct _next_receiver<Operation>::type {
  using next_receiver = type;
  using state_type = typename Operation::state_type;
  using receiver_type = typename Operation::receiver_type;
  Operation& op_;

  template <
      typename CPO,
      std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
  friend auto tag_invoke(CPO cpo, const next_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const receiver_type&>)
      -> callable_result_t<CPO, const receiver_type&> {
    return std::move(cpo)(std::as_const(r.op_.receiver_));
  }

  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const next_receiver& r,
      Func&& func) {
    std::invoke(func, r.op_.receiver_);
  }

  template <typename... Values>
  void set_value(Values... values) && noexcept {
    auto& op = op_;
    op.next_.destruct();
    try {
      op.state_ = std::invoke(
          op.reducer_,
          std::forward<state_type>(op.state_),
          (Values &&) values...);
      op.next_.construct_from([&]() {
        return unifex::connect(next(op.stream_), _reduce::next_receiver<Operation>{op});
      });
      unifex::start(op.next_.get());
    } catch (...) {
      op.errorCleanup_.construct_from([&] {
        return unifex::connect(
            cleanup(op.stream_),
            error_cleanup_receiver<Operation>{op, std::current_exception()});
      });
      unifex::start(op.errorCleanup_.get());
    }
  }

  void set_done() && noexcept {
    auto& op = op_;
    op.next_.destruct();
    op.doneCleanup_.construct_from([&]() {
      return unifex::connect(
          cleanup(op.stream_), done_cleanup_receiver<Operation>{op});
    });
    unifex::start(op.doneCleanup_.get());
  }

  void set_error(std::exception_ptr ex) && noexcept {
    auto& op = op_;
    op.next_.destruct();
    op.errorCleanup_.construct_from([&]() {
      return unifex::connect(
          cleanup(op.stream_),
          error_cleanup_receiver<Operation>{op, std::move(ex)});
    });
    unifex::start(op.errorCleanup_.get());
  }

  template <typename Error>
  void set_error(Error&& e) && noexcept {
    std::move(*this).set_error(std::make_exception_ptr((Error &&) e));
  }
};

template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
struct _op {
  struct type;
};
template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
using operation =
    typename _op<std::remove_cvref_t<StreamSender>, std::remove_cvref_t<State>, std::remove_cvref_t<ReducerFunc>, std::remove_cvref_t<Receiver>>::type;

template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
struct _op<StreamSender, State, ReducerFunc, Receiver>::type {
  using operation = type;
  using state_type = State;
  using receiver_type = Receiver;
  UNIFEX_NO_UNIQUE_ADDRESS StreamSender stream_;
  UNIFEX_NO_UNIQUE_ADDRESS state_type state_;
  UNIFEX_NO_UNIQUE_ADDRESS ReducerFunc reducer_;
  UNIFEX_NO_UNIQUE_ADDRESS receiver_type receiver_;

  using next_op = manual_lifetime<next_operation_t<StreamSender, next_receiver<operation>>>;
  using error_op = manual_lifetime<cleanup_operation_t<StreamSender, error_cleanup_receiver<operation>>>;
  using done_op = manual_lifetime<cleanup_operation_t<StreamSender, done_cleanup_receiver<operation>>>;
  union {
    next_op next_;
    error_op errorCleanup_;
    done_op doneCleanup_;
  };

  template <typename StreamSender2, typename State2, typename ReducerFunc2, typename Receiver2>
  explicit type(
      StreamSender2&& stream,
      State2&& state,
      ReducerFunc2&& reducer,
      Receiver2&& receiver)
    : stream_(std::forward<StreamSender2>(stream)),
      state_(std::forward<State2>(state)),
      reducer_(std::forward<ReducerFunc2>(reducer)),
      receiver_(std::forward<Receiver2>(receiver)) {}

  ~type() {} // Due to the union member, this is load-bearing. DO NOT DELETE.

  void start() noexcept {
    try {
      next_.construct_from([&]() {
        return unifex::connect(next(stream_), next_receiver<operation>{*this});
      });
      unifex::start(next_.get());
    } catch (...) {
      unifex::set_error(
          static_cast<Receiver&&>(receiver_), std::current_exception());
    }
  }
};

template <typename StreamSender, typename State, typename ReducerFunc>
struct _sender {
  struct type;
};
template <typename StreamSender, typename State, typename ReducerFunc>
using sender = typename _sender<
    std::remove_cvref_t<StreamSender>,
    std::remove_cvref_t<State>,
    std::remove_cvref_t<ReducerFunc>>::type;

template <typename StreamSender, typename State, typename ReducerFunc>
struct _sender<StreamSender, State, ReducerFunc>::type {
  using sender = type;
  StreamSender stream_;
  State initialState_;
  ReducerFunc reducer_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<State>>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      typename next_sender_t<StreamSender>::template error_types<type_list>,
      typename cleanup_sender_t<StreamSender>::template error_types<type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  template<typename Receiver>
  using operation = operation<StreamSender, State, ReducerFunc, Receiver>;

  template <typename Receiver>
  operation<Receiver> connect(Receiver&& receiver) && {
    return operation<Receiver>{
        (StreamSender &&) stream_,
        (State &&) initialState_,
        (ReducerFunc &&) reducer_,
        (Receiver &&) receiver};
  }

  template <typename Receiver>
  operation<Receiver> connect(Receiver&& receiver) & {
    return operation<Receiver>{
        stream_,
        initialState_,
        reducer_,
        (Receiver &&) receiver};
  }

  template <typename Receiver>
  operation<Receiver> connect(Receiver&& receiver) const& {
    return operation<Receiver>{
        stream_,
        initialState_,
        reducer_,
        (Receiver &&) receiver};
  }
};
} // namespace _reduce

namespace _reduce_cpo {
  inline constexpr struct _fn {
    template <typename StreamSender, typename State, typename ReducerFunc>
    auto operator()(
        StreamSender&& stream,
        State&& initialState,
        ReducerFunc&& reducer) const
        noexcept(std::is_nothrow_constructible_v<
            _reduce::sender<StreamSender, State, ReducerFunc>,
            StreamSender, State, ReducerFunc>)
        -> _reduce::sender<StreamSender, State, ReducerFunc> {
      return _reduce::sender<StreamSender, State, ReducerFunc>{
          (StreamSender &&) stream,
          (State &&) initialState,
          (ReducerFunc &&) reducer};
    }
  } reduce_stream{};
} // namespace _reduce_cpo
using _reduce_cpo::reduce_stream;
} // namespace unifex
