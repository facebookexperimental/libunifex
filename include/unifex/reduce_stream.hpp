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
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/bind_back.hpp>
#include <unifex/exception.hpp>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _reduce {
template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
struct _op {
  struct type;
};
template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
using operation =
    typename _op<
        remove_cvref_t<StreamSender>,
        remove_cvref_t<State>,
        remove_cvref_t<ReducerFunc>,
        remove_cvref_t<Receiver>>::type;

template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
struct _error_cleanup_receiver {
  struct type;
};
template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
using error_cleanup_receiver =
    typename _error_cleanup_receiver<
        remove_cvref_t<StreamSender>,
        remove_cvref_t<State>,
        remove_cvref_t<ReducerFunc>,
        remove_cvref_t<Receiver>>::type;

template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
struct _error_cleanup_receiver<StreamSender, State, ReducerFunc, Receiver>::type {
  operation<StreamSender, State, ReducerFunc, Receiver>& op_;
  std::exception_ptr ex_;

  // No value() in cleanup receiver

  template <typename Error>
  void set_error(Error error) noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.errorCleanup_);
    unifex::set_error(static_cast<Receiver&&>(op.receiver_), (Error &&) error);
  }

  void set_done() noexcept {
    auto& op = op_;
    auto ex = std::move(ex_);
    unifex::deactivate_union_member(op.errorCleanup_);
    unifex::set_error(static_cast<Receiver&&>(op.receiver_), std::move(ex));
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const type& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.op_.receiver_));
  }

  friend unstoppable_token tag_invoke(tag_t<get_stop_token>, const type&) noexcept {
    return {};
  }

  template <typename Func>
  friend void tag_invoke(tag_t<visit_continuations>, const type& r, Func&& func) {
    std::invoke(func, r.op_.receiver_);
  }
};

template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
struct _done_cleanup_receiver {
  struct type;
};
template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
using done_cleanup_receiver =
    typename _done_cleanup_receiver<
        remove_cvref_t<StreamSender>,
        remove_cvref_t<State>,
        remove_cvref_t<ReducerFunc>,
        remove_cvref_t<Receiver>>::type;

template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
struct _done_cleanup_receiver<StreamSender, State, ReducerFunc, Receiver>::type {
  operation<StreamSender, State, ReducerFunc, Receiver>& op_;

  template <typename Error>
  void set_error(Error error) && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.doneCleanup_);
    unifex::set_error(static_cast<Receiver&&>(op.receiver_), (Error &&) error);
  }

  void set_done() && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.doneCleanup_);
    unifex::set_value(
        static_cast<Receiver&&>(op.receiver_),
        std::forward<State>(op.state_));
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const type& r)
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.op_.receiver_));
  }

  friend unstoppable_token tag_invoke(tag_t<get_stop_token>, const type&) noexcept {
    return {};
  }

  template <typename Func>
  friend void tag_invoke(tag_t<visit_continuations>, const type& r, Func&& func) {
    std::invoke(func, r.op_.receiver_);
  }
};

template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
struct _next_receiver {
  struct type;
};
template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
using next_receiver =
    typename _next_receiver<
        remove_cvref_t<StreamSender>,
        remove_cvref_t<State>,
        remove_cvref_t<ReducerFunc>,
        remove_cvref_t<Receiver>>::type;

template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
struct _next_receiver<StreamSender, State, ReducerFunc, Receiver>::type {
  using error_cleanup_receiver_t = error_cleanup_receiver<StreamSender, State, ReducerFunc, Receiver>;
  using done_cleanup_receiver_t = done_cleanup_receiver<StreamSender, State, ReducerFunc, Receiver>;
  using next_receiver_t = next_receiver<StreamSender, State, ReducerFunc, Receiver>;
  operation<StreamSender, State, ReducerFunc, Receiver>& op_;

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const type& r)
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.op_.receiver_));
  }

  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      Func&& func) {
    std::invoke(func, r.op_.receiver_);
  }

  template <typename... Values>
  void set_value(Values... values) && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.next_);
    UNIFEX_TRY {
      op.state_ = std::invoke(
          op.reducer_,
          std::forward<State>(op.state_),
          (Values &&) values...);
      unifex::activate_union_member_with(op.next_, [&] {
        return unifex::connect(next(op.stream_), next_receiver_t{op});
      });
      unifex::start(op.next_.get());
    } UNIFEX_CATCH (...) {
      unifex::activate_union_member_with(op.errorCleanup_, [&] {
        return unifex::connect(
            cleanup(op.stream_),
            error_cleanup_receiver_t{op, std::current_exception()});
      });
      unifex::start(op.errorCleanup_.get());
    }
  }

  void set_done() && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.next_);
    unifex::activate_union_member_with(op.doneCleanup_, [&] {
      return unifex::connect(
          cleanup(op.stream_), done_cleanup_receiver_t{op});
    });
    unifex::start(op.doneCleanup_.get());
  }

  void set_error(std::exception_ptr ex) && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.next_);
    unifex::activate_union_member_with(op.errorCleanup_, [&] {
      return unifex::connect(
          cleanup(op.stream_),
          error_cleanup_receiver_t{op, std::move(ex)});
    });
    unifex::start(op.errorCleanup_.get());
  }

  template <typename Error>
  void set_error(Error&& e) && noexcept {
    std::move(*this).set_error(make_exception_ptr((Error &&) e));
  }
};

template <typename StreamSender, typename State, typename ReducerFunc, typename Receiver>
struct _op<StreamSender, State, ReducerFunc, Receiver>::type {
  UNIFEX_NO_UNIQUE_ADDRESS StreamSender stream_;
  UNIFEX_NO_UNIQUE_ADDRESS State state_;
  UNIFEX_NO_UNIQUE_ADDRESS ReducerFunc reducer_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  using next_receiver_t = next_receiver<StreamSender, State, ReducerFunc, Receiver>;
  using error_cleanup_receiver_t = error_cleanup_receiver<StreamSender, State, ReducerFunc, Receiver>;
  using done_cleanup_receiver_t = done_cleanup_receiver<StreamSender, State, ReducerFunc, Receiver>;

  using next_op = manual_lifetime<next_operation_t<StreamSender, next_receiver_t>>;
  using error_op = manual_lifetime<cleanup_operation_t<StreamSender, error_cleanup_receiver_t>>;
  using done_op = manual_lifetime<cleanup_operation_t<StreamSender, done_cleanup_receiver_t>>;

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
    UNIFEX_TRY {
      unifex::activate_union_member_with(next_, [&] {
        return unifex::connect(next(stream_), next_receiver_t{*this});
      });
      unifex::start(next_.get());
    } UNIFEX_CATCH (...) {
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
    remove_cvref_t<StreamSender>,
    remove_cvref_t<State>,
    remove_cvref_t<ReducerFunc>>::type;

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
  using error_types =
      typename concat_type_lists_unique_t<
          sender_error_types_t<next_sender_t<StreamSender>, type_list>,
          sender_error_types_t<cleanup_sender_t<StreamSender>, type_list>,
          type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = false;

  template <typename Receiver>
  using operation_t = operation<StreamSender, State, ReducerFunc, Receiver>;
  template <typename Receiver>
  using next_receiver_t = next_receiver<StreamSender, State, ReducerFunc, Receiver>;
  template <typename Receiver>
  using error_cleanup_receiver_t = error_cleanup_receiver<StreamSender, State, ReducerFunc, Receiver>;
  template <typename Receiver>
  using done_cleanup_receiver_t = done_cleanup_receiver<StreamSender, State, ReducerFunc, Receiver>;

  template (typename Self, typename Receiver)
      (requires same_as<remove_cvref_t<Self>, type> AND receiver<Receiver> AND
          sender_to<next_sender_t<StreamSender>, next_receiver_t<Receiver>> AND
          sender_to<cleanup_sender_t<StreamSender>, error_cleanup_receiver_t<Receiver>> AND
          sender_to<cleanup_sender_t<StreamSender>, done_cleanup_receiver_t<Receiver>>)
  friend operation_t<Receiver> tag_invoke(tag_t<connect>, Self&& self, Receiver&& receiver) {
    return operation_t<Receiver>{
        static_cast<Self&&>(self).stream_,
        static_cast<Self&&>(self).initialState_,
        static_cast<Self&&>(self).reducer_,
        (Receiver &&) receiver};
  }
};
} // namespace _reduce

namespace _reduce_cpo {
  inline const struct _fn {
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
    template <typename State, typename ReducerFunc>
    constexpr auto operator()(State&& initialState, ReducerFunc&& reducer) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, State, ReducerFunc>)
        -> bind_back_result_t<_fn, State, ReducerFunc> {
      return bind_back(*this, (State&&)initialState, (ReducerFunc&&)reducer);
    }
  } reduce_stream{};
} // namespace _reduce_cpo
using _reduce_cpo::reduce_stream;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
