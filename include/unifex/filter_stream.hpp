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

#include <unifex/bind_back.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/stream_concepts.hpp>

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
  void set_value(Values&&... values) && noexcept {
    auto& op = op_;
    op.next_.destruct();
    UNIFEX_TRY {
      const bool doFilter = !std::invoke(op.filter_, std::as_const(values)...);

      if (doFilter) {
        op.next_.destruct();
        op.nextEngaged_ = false;
        op.next_.construct_with([&] {
          return unifex::connect(next(op.stream_), next_receiver_t{op});
        });
        unifex::start(op.next_.get());
      } else {
        unifex::set_value(
            std::move(op.receiver_), std::forward<Values>(values)...);
      }
    }
    UNIFEX_CATCH(...) {
      unifex::set_error(std::move(op.receiver_), std::current_exception());
    }
  }

  void set_done() && noexcept { unifex::set_done(std::move(op_.receiver_)); }

  template <typename Error>
  void set_error(Error&& e) && noexcept {
    unifex::set_error(std::move(op_.receiver_), std::forward<Error>(e));
  }
};

template <typename StreamSender, typename FilterFunc, typename Receiver>
struct _op<StreamSender, FilterFunc, Receiver>::type {
  StreamSender& stream_;
  FilterFunc filter_;
  Receiver receiver_;

  using next_receiver_t = next_receiver<StreamSender, FilterFunc, Receiver>;
  using next_op =
      manual_lifetime<next_operation_t<StreamSender, next_receiver_t>>;

  next_op next_;
  bool nextEngaged_{false};

  template <typename StreamSender2, typename FilterFunc2, typename Receiver2>
  explicit type(
      StreamSender2&& stream, FilterFunc2&& filter, Receiver2&& receiver)
    : stream_(std::forward<StreamSender2>(stream))
    , filter_(std::forward<FilterFunc2>(filter))
    , receiver_(std::forward<Receiver2>(receiver)) {}

  // Question to @ispeters: This didn't make a lot of sense to me, but now that
  // we don't have a union, do we want to remove this?
  ~type() {}  // Due to the union member, this is load-bearing. DO NOT DELETE.

  void start() noexcept {
    UNIFEX_TRY {
      next_.construct_with([&] {
        return unifex::connect(next(stream_), next_receiver_t{*this});
      });
      nextEngaged_ = true;
      unifex::start(next_.get());
    }
    UNIFEX_CATCH(...) {
      unifex::set_error(std::move(receiver_), std::current_exception());
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
  using value_types =
      sender_value_types_t<next_sender_t<StreamSender>, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<next_sender_t<StreamSender>, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done =
      sender_traits<next_sender_t<StreamSender>>::sends_done;

  template <typename Receiver>
  using operation_t = operation<StreamSender, FilterFunc, Receiver>;
  template <typename Receiver>
  using next_receiver_t = next_receiver<StreamSender, FilterFunc, Receiver>;

  template(typename Self, typename Receiver)  //
      (requires same_as<remove_cvref_t<Self>, type> AND receiver<Receiver> AND
           sender_to<
               next_sender_t<StreamSender>,
               next_receiver_t<Receiver>>)  //
      friend operation_t<Receiver> tag_invoke(
          tag_t<connect>, Self&& self, Receiver&& receiver) {
    return operation_t<Receiver>{
        self.stream_,
        std::move(self).filter_,
        std::forward<Receiver>(receiver)};
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

  friend auto tag_invoke(tag_t<next>, type& s) noexcept(
      std::is_nothrow_constructible_v<
          _filter::sender<StreamSender, FilterFunc>,
          StreamSender&,
          FilterFunc&>) {
    return _filter::sender<StreamSender, FilterFunc>{s.stream_, s.filter_};
  }
  friend auto
  tag_invoke(tag_t<cleanup>, type& s) noexcept(noexcept(cleanup(s.stream_))) {
    return cleanup(s.stream_);
  }
};

template <typename StreamSender, typename FilterFunc>
using filter_stream = typename _filter_stream<
    remove_cvref_t<StreamSender>,
    remove_cvref_t<FilterFunc>>::type;

struct _fn {
  template <typename StreamSender, typename FilterFunc>
  auto operator()(StreamSender&& stream, FilterFunc&& filterFunc) const
      noexcept(std::is_nothrow_constructible_v<
               filter_stream<StreamSender, FilterFunc>,
               StreamSender,
               FilterFunc>) -> filter_stream<StreamSender, FilterFunc> {
    return filter_stream<StreamSender, FilterFunc>{
        std::forward<StreamSender>(stream),
        std::forward<FilterFunc>(filterFunc)};
  }

  template <typename FilterFunc>
  constexpr auto operator()(FilterFunc&& filterFunc) const
      noexcept(is_nothrow_callable_v<tag_t<bind_back>, _fn, FilterFunc>)
          -> bind_back_result_t<_fn, FilterFunc> {
    return bind_back(*this, std::forward<FilterFunc>(filterFunc));
  }
};

}  // namespace _filter_stream

inline constexpr _filter_stream::_fn filter_stream{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
