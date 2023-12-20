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
#include <unifex/bind_back.hpp>
#include <unifex/exception.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/unstoppable_token.hpp>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _filter {
template <typename StreamSender, typename FilterFunc, typename Receiver>
struct _op {
  struct type;
};
template <typename StreamSender, typename FilterFunc, typename Receiver>
using operation = typename _op<
    remove_cvref_t<StreamSender>,
    remove_cvref_t<FilterFunc>,
    remove_cvref_t<Receiver>>::type;

template <typename StreamSender, typename FilterFunc, typename Receiver>
struct _error_cleanup_receiver {
  struct type;
};
template <typename StreamSender, typename FilterFunc, typename Receiver>
using error_cleanup_receiver = typename _error_cleanup_receiver<
    remove_cvref_t<StreamSender>,
    remove_cvref_t<FilterFunc>,
    remove_cvref_t<Receiver>>::type;

template <typename StreamSender, typename FilterFunc, typename Receiver>
struct _error_cleanup_receiver<StreamSender, FilterFunc, Receiver>::type {
  operation<StreamSender, FilterFunc, Receiver>& op_;
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

  template(typename CPO)                       //
      (requires is_receiver_query_cpo_v<CPO>)  //
      friend auto tag_invoke(CPO cpo, const type& r) noexcept(
          is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.op_.receiver_));
  }

  friend unstoppable_token
  tag_invoke(tag_t<get_stop_token>, const type&) noexcept {
    return {};
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void
  tag_invoke(tag_t<visit_continuations>, const type& r, Func&& func) {
    std::invoke(func, r.op_.receiver_);
  }
#endif
};

template <typename StreamSender, typename FilterFunc, typename Receiver>
struct _done_cleanup_receiver {
  struct type;
};
template <typename StreamSender, typename FilterFunc, typename Receiver>
using done_cleanup_receiver = typename _done_cleanup_receiver<
    remove_cvref_t<StreamSender>,
    remove_cvref_t<FilterFunc>,
    remove_cvref_t<Receiver>>::type;

template <typename StreamSender, typename FilterFunc, typename Receiver>
struct _done_cleanup_receiver<StreamSender, FilterFunc, Receiver>::type {
  operation<StreamSender, FilterFunc, Receiver>& op_;

  template <typename Error>
  void set_error(Error error) && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.doneCleanup_);
    unifex::set_error(static_cast<Receiver&&>(op.receiver_), (Error &&) error);
  }

  void set_done() && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.doneCleanup_);
    unifex::set_done(std::move(op.receiver_));
  }

  template(typename CPO)                       //
      (requires is_receiver_query_cpo_v<CPO>)  //
      friend auto tag_invoke(CPO cpo, const type& r) noexcept(
          is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.op_.receiver_));
  }

  friend unstoppable_token
  tag_invoke(tag_t<get_stop_token>, const type&) noexcept {
    return {};
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void
  tag_invoke(tag_t<visit_continuations>, const type& r, Func&& func) {
    std::invoke(func, r.op_.receiver_);
  }
#endif
};

template <typename StreamSender, typename FilterFunc, typename Receiver>
struct _next_receiver {
  struct type;
};
template <typename StreamSender, typename FilterFunc, typename Receiver>
using next_receiver = typename _next_receiver<
    remove_cvref_t<StreamSender>,
    remove_cvref_t<FilterFunc>,
    remove_cvref_t<Receiver>>::type;

template <typename StreamSender, typename FilterFunc, typename Receiver>
struct _next_receiver<StreamSender, FilterFunc, Receiver>::type {
  using error_cleanup_receiver_t =
      error_cleanup_receiver<StreamSender, FilterFunc, Receiver>;
  using done_cleanup_receiver_t =
      done_cleanup_receiver<StreamSender, FilterFunc, Receiver>;
  using next_receiver_t = next_receiver<StreamSender, FilterFunc, Receiver>;
  operation<StreamSender, FilterFunc, Receiver>& op_;

  template(typename CPO)                       //
      (requires is_receiver_query_cpo_v<CPO>)  //
      friend auto tag_invoke(CPO cpo, const type& r) noexcept(
          is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.op_.receiver_));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void
  tag_invoke(tag_t<visit_continuations>, const type& r, Func&& func) {
    std::invoke(func, r.op_.receiver_);
  }
#endif

  template <typename... Values>
  void set_value(Values... values) && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.next_);
    UNIFEX_TRY {
      const bool doFilter = !std::invoke(op.filter_, (Values &&) values...);

      if (doFilter) {
        unifex::activate_union_member_with(op.next_, [&] {
          return unifex::connect(next(op.stream_), next_receiver_t{op});
        });
        unifex::start(op.next_.get());
      } else {
        unifex::set_value(std::move(op.receiver_), std::move(values)...);
      }
    }
    UNIFEX_CATCH(...) {
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
      return unifex::connect(cleanup(op.stream_), done_cleanup_receiver_t{op});
    });
    unifex::start(op.doneCleanup_.get());
  }

  void set_error(std::exception_ptr ex) && noexcept {
    auto& op = op_;
    unifex::deactivate_union_member(op.next_);
    unifex::activate_union_member_with(op.errorCleanup_, [&] {
      return unifex::connect(
          cleanup(op.stream_), error_cleanup_receiver_t{op, std::move(ex)});
    });
    unifex::start(op.errorCleanup_.get());
  }

  template <typename Error>
  void set_error(Error&& e) && noexcept {
    std::move(*this).set_error(make_exception_ptr((Error &&) e));
  }
};

template <typename StreamSender, typename FilterFunc, typename Receiver>
struct _op<StreamSender, FilterFunc, Receiver>::type {
  StreamSender& stream_;
  FilterFunc filter_;
  Receiver receiver_;

  using next_receiver_t = next_receiver<StreamSender, FilterFunc, Receiver>;
  using error_cleanup_receiver_t =
      error_cleanup_receiver<StreamSender, FilterFunc, Receiver>;
  using done_cleanup_receiver_t =
      done_cleanup_receiver<StreamSender, FilterFunc, Receiver>;

  using next_op =
      manual_lifetime<next_operation_t<StreamSender, next_receiver_t>>;
  using error_op = manual_lifetime<
      cleanup_operation_t<StreamSender, error_cleanup_receiver_t>>;
  using done_op = manual_lifetime<
      cleanup_operation_t<StreamSender, done_cleanup_receiver_t>>;

  union {
    next_op next_;
    error_op errorCleanup_;
    done_op doneCleanup_;
  };

  template <typename StreamSender2, typename FilterFunc2, typename Receiver2>
  explicit type(
      StreamSender2&& stream, FilterFunc2&& filter, Receiver2&& receiver)
    : stream_(std::forward<StreamSender2>(stream))
    , filter_(std::forward<FilterFunc2>(filter))
    , receiver_(std::forward<Receiver2>(receiver)) {}

  ~type() {}  // Due to the union member, this is load-bearing. DO NOT DELETE.

  void start() noexcept {
    UNIFEX_TRY {
      unifex::activate_union_member_with(next_, [&] {
        return unifex::connect(next(stream_), next_receiver_t{*this});
      });
      unifex::start(next_.get());
    }
    UNIFEX_CATCH(...) {
      unifex::set_error(
          static_cast<Receiver&&>(receiver_), std::current_exception());
    }
  }
};

template <typename StreamSender, typename FilterFunc>
struct _sender {
  struct type;
};
template <typename StreamSender, typename FilterFunc>
using sender =
    typename _sender<remove_cvref_t<StreamSender>, remove_cvref_t<FilterFunc>>::
        type;

template <typename StreamSender, typename FilterFunc>
struct _sender<StreamSender, FilterFunc>::type {
  using sender = type;
  StreamSender& stream_;
  FilterFunc& filter_;

  // value and error types just point to the ones from the stream sender
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = typename next_sender_t<
      StreamSender>::template value_types<Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types =
      typename next_sender_t<StreamSender>::template error_types<Variant>;

  static constexpr bool sends_done = false;

  template <typename Receiver>
  using operation_t = operation<StreamSender, FilterFunc, Receiver>;
  template <typename Receiver>
  using next_receiver_t = next_receiver<StreamSender, FilterFunc, Receiver>;
  template <typename Receiver>
  using error_cleanup_receiver_t =
      error_cleanup_receiver<StreamSender, FilterFunc, Receiver>;
  template <typename Receiver>
  using done_cleanup_receiver_t =
      done_cleanup_receiver<StreamSender, FilterFunc, Receiver>;

  template(typename Self, typename Receiver)  //
      (requires same_as<remove_cvref_t<Self>, type> AND receiver<Receiver> AND
           sender_to<next_sender_t<StreamSender>, next_receiver_t<Receiver>> AND
               sender_to<
                   cleanup_sender_t<StreamSender>,
                   error_cleanup_receiver_t<Receiver>> AND
                   sender_to<
                       cleanup_sender_t<StreamSender>,
                       done_cleanup_receiver_t<Receiver>>)  //
      friend operation_t<Receiver> tag_invoke(
          tag_t<connect>, Self&& self, Receiver&& receiver) {
    return operation_t<Receiver>{
        self.stream_,
        static_cast<Self&&>(self).filter_,
        (Receiver &&) receiver};
  }
};
}  // namespace _filter

namespace _filter_stream {

template <typename StreamSender, typename FilterFunc>
struct _filter_stream {
  struct type;
};

template <typename StreamSender, typename FilterFunc>
struct _filter_stream<StreamSender, FilterFunc>::type {
  StreamSender stream_;
  FilterFunc filter_;

  friend auto tag_invoke(tag_t<next>, type& s) {
    return _filter::sender<StreamSender, FilterFunc>{s.stream_, s.filter_};
  }

  friend auto tag_invoke(tag_t<cleanup>, type& s) { return cleanup(s.stream_); }
};

struct _fn {
  // TODO add a type requirement that filterFunc's return is boolean
  template <typename StreamSender, typename FilterFunc>
  auto operator()(StreamSender&& stream, FilterFunc&& filterFunc) const {
    return typename _filter_stream<StreamSender, FilterFunc>::type{
        (StreamSender &&) stream, (FilterFunc &&) filterFunc};
  }

  template <typename FilterFunc>
  constexpr auto operator()(FilterFunc&& filterFunc) const
      noexcept(is_nothrow_callable_v<tag_t<bind_back>, _fn, FilterFunc>)
          -> bind_back_result_t<_fn, FilterFunc> {
    return bind_back(*this, (FilterFunc &&) filterFunc);
  }
};

}  // namespace _filter_stream

inline constexpr _filter_stream::_fn filter_stream{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
