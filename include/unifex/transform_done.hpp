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
#include <unifex/tag_invoke.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/manual_lifetime_union.hpp>
#include <unifex/async_trace.hpp>

#include <utility>
#include <cassert>
#include <exception>

namespace unifex {

namespace _transform_done {
template<typename Source, typename Done, typename Receiver>
struct _op {
  class type;
};
template<typename Source, typename Done, typename Receiver>
using operation_type = typename _op<Source, Done, std::remove_cvref_t<Receiver>>::type;

template<typename Source, typename Done, typename Receiver>
struct _rcvr {
  class type;
};
template<typename Source, typename Done, typename Receiver>
using receiver_type = typename _rcvr<Source, Done, std::remove_cvref_t<Receiver>>::type;

template<typename Source, typename Done, typename Receiver>
struct _frcvr {
  class type;
};
template<typename Source, typename Done, typename Receiver>
using final_receiver_type = typename _frcvr<Source, Done, std::remove_cvref_t<Receiver>>::type;

template<typename Source, typename Done>
struct _sndr {
  class type;
};

template<typename Source, typename Done, typename Receiver>
class _rcvr<Source, Done, Receiver>::type {
  using operation = operation_type<Source, Done, Receiver>;
  using final_receiver = final_receiver_type<Source, Done, Receiver>;
  using final_sender_t = callable_result_t<Done&>;

public:
  explicit type(operation* op) noexcept
  : op_(op) {}

  type(type&& other) noexcept
  : op_(std::exchange(other.op_, {}))
  {}

  template<typename... Values>
  void set_value(Values&&... values) noexcept(is_nothrow_callable_v<tag_t<unifex::set_value>&, Receiver, Values...>) {
    assert(op_ != nullptr);
    unifex::set_value(std::move(op_->receiver_), (Values&&)values...);
  }

  template<
    typename R = Receiver,
    std::enable_if_t<is_callable_v<decltype(unifex::set_done), R>, int> = 0>
  void set_done() noexcept {
    assert(op_ != nullptr);
    auto op = op_; // preserve pointer value.
    if constexpr (
      is_nothrow_callable_v<Done> &&
      is_nothrow_connectable_v<final_sender_t, final_receiver>) {
      op->startedOp_ = 0;
      op->sourceOp_.destruct();
      op->finalOp_.construct_from([&] {
        return unifex::connect(std::move(op->done_)(), final_receiver{op});
      });
      op->startedOp_ = 0 - 1;
      unifex::start(op->finalOp_.get());
    } else {
      try {
        op->startedOp_ = 0;
        op->sourceOp_.destruct();
        op->finalOp_.construct_from([&] {
          return unifex::connect(std::move(op->done_)(), final_receiver{op});
        });
        op->startedOp_ = 0 - 1;
        unifex::start(op->finalOp_.get());
      } catch (...) {
        unifex::set_error(std::move(op->receiver_), std::current_exception());
      }
    }
  }

  template<
    typename Error,
    std::enable_if_t<is_callable_v<decltype(unifex::set_error), Receiver, Error>, int> = 0>
  void set_error(Error&& error) noexcept {
    assert(op_ != nullptr);
    unifex::set_error(std::move(op_->receiver_), (Error&&)error);
  }

private:
  template<
    typename CPO,
    typename Self,
    std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0,
    std::enable_if_t<std::is_same_v<std::remove_cvref_t<Self>, type>, int> = 0,
    std::enable_if_t<is_callable_v<CPO, const Receiver&>, int> = 0>
  friend auto tag_invoke(CPO cpo, Self&& r)
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(r.get_receiver());
  }

  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      VisitFunc&& func) noexcept(is_nothrow_callable_v<
                                VisitFunc&,
                                const Receiver&>) {
    func(r.get_receiver());
  }

  const Receiver& get_receiver() const noexcept {
    assert(op_ != nullptr);
    return op_->receiver_;
  }

  operation* op_;
};

template<typename Source, typename Done, typename Receiver>
class _frcvr<Source, Done, Receiver>::type {
  using operation = operation_type<Source, Done, Receiver>;

public:
  explicit type(operation* op) noexcept
  : op_(op) {}

  type(type&& other) noexcept
  : op_(std::exchange(other.op_, {}))
  {}

  template<
    typename... Values,
    std::enable_if_t<is_callable_v<decltype(unifex::set_value), Receiver, Values...>, int> = 0>
  void set_value(Values&&... values) noexcept(is_nothrow_callable_v<tag_t<unifex::set_value>&, Receiver, Values...>) {
    assert(op_ != nullptr);
    unifex::set_value(std::move(op_->receiver_));
  }

  template<
    typename R = Receiver,
    std::enable_if_t<is_callable_v<decltype(unifex::set_done), R>, int> = 0>
  void set_done() noexcept {
    assert(op_ != nullptr);
    unifex::set_done(std::move(op_->receiver_));
  }

  template<
    typename Error,
    std::enable_if_t<is_callable_v<decltype(unifex::set_error), Receiver, Error>, int> = 0>
  void set_error(Error&& error) noexcept {
    assert(op_ != nullptr);
    unifex::set_error(std::move(op_->receiver_), (Error&&)error);
  }

private:
  template<
    typename CPO,
    std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0,
    std::enable_if_t<is_callable_v<CPO, const Receiver&>, int> = 0>
  friend auto tag_invoke(CPO cpo, const type& r)
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(r.get_receiver());
  }

  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      VisitFunc&& func) noexcept(is_nothrow_callable_v<
                                VisitFunc&,
                                const Receiver&>) {
    func(r.get_receiver());
  }

  const Receiver& get_receiver() const noexcept {
    assert(op_ != nullptr);
    return op_->receiver_;
  }

  operation* op_;
};

template<typename Source, typename Done, typename Receiver>
class _op<Source, Done, Receiver>::type {
  using source_receiver = receiver_type<Source, Done, Receiver>;
  using final_receiver = final_receiver_type<Source, Done, Receiver>;

public:
  template<typename Done2, typename Receiver2>
  explicit type(Source&& source, Done2&& done, Receiver2&& dest)
      noexcept(std::is_nothrow_move_constructible_v<Receiver> &&
               std::is_nothrow_move_constructible_v<Done> &&
               is_nothrow_connectable_v<Source, source_receiver>)
  : done_((Done2&&)done)
  , receiver_((Receiver2&&)dest)
  {
    sourceOp_.construct_from([&] {
        return unifex::connect((Source&&)source, source_receiver{this});
      });
    startedOp_ = 0 + 1;
  }

  ~type() {
    if (startedOp_ < 0) {
      finalOp_.destruct();
    } else if (startedOp_ > 0) {
      sourceOp_.destruct();
    }
    startedOp_ = 0;
  }

  void start() & noexcept {
    unifex::start(sourceOp_.get());
  }

private:
  friend source_receiver;
  friend final_receiver;

  using source_op_t = operation_t<Source, source_receiver>;

  using final_sender_t = callable_result_t<Done>;

  using final_op_t = operation_t<final_sender_t, final_receiver>;

  UNIFEX_NO_UNIQUE_ADDRESS Done done_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  int startedOp_ = 0;
  union {
    manual_lifetime<source_op_t> sourceOp_;
    manual_lifetime<final_op_t> finalOp_;
  };
};

template<typename Source, typename Done>
class _sndr<Source, Done>::type {
  using final_sender_t = callable_result_t<Done>;

public:
  template<template<typename...> class Variant,
           template<typename...> class Tuple>
  using value_types = typename concat_type_lists_unique_t<
        typename Source::template value_types<type_list, Tuple>,
        typename decltype(std::declval<Done>()())::template value_types<type_list, Tuple>
      >::template apply<Variant>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      typename Source::template error_types<type_list>,
      typename decltype(std::declval<Done>()())::template error_types<type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  template<typename Source2, typename Done2>
  explicit type(Source2&& source, Done2&& done)
    noexcept(
      std::is_nothrow_constructible_v<Source, Source2> &&
      std::is_nothrow_constructible_v<Done, Done2>)
    : source_((Source2&&)source)
    , done_((Done2&&)done)
  {}

  template<
    typename Sender,
    typename Receiver,
    typename SourceReceiver = receiver_type<member_t<Sender, Source>, Done, Receiver>,
    typename FinalReceiver = final_receiver_type<member_t<Sender, Source>, Done, Receiver>,
    std::enable_if_t<std::is_same_v<remove_cvref_t<Sender>, type>, int> = 0,
    std::enable_if_t<std::is_constructible_v<Done, member_t<Sender, Done>>, int> = 0,
    std::enable_if_t<std::is_constructible_v<remove_cvref_t<Receiver>, Receiver>, int> = 0,
    std::enable_if_t<is_connectable_v<member_t<Sender, Source>, SourceReceiver>, int> = 0,
    std::enable_if_t<is_connectable_v<final_sender_t, FinalReceiver>, int> = 0>
  friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r)
       noexcept(
        is_nothrow_connectable_v<member_t<Sender, Source>, SourceReceiver> &&
        std::is_nothrow_constructible_v<Done, member_t<Sender, Done>> &&
        std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>)
      -> operation_type<member_t<Sender, Source>, Done, Receiver> {
    return operation_type<member_t<Sender, Source>, Done, Receiver>{
      static_cast<Sender&&>(s).source_,
      static_cast<Sender&&>(s).done_,
      static_cast<Receiver&&>(r)
    };
  }

private:
  Source source_;
  Done done_;
};

} // namespace _transform_done

template<class Source, class Done>
using transform_done_sender = typename _transform_done::_sndr<std::remove_cvref_t<Source>, std::remove_cvref_t<Done>>::type;

inline constexpr struct transform_done_cpo {
  template<typename Source, typename Done>
  auto operator()(Source&& source, Done&& done) const
      noexcept(is_nothrow_tag_invocable_v<transform_done_cpo, Source, Done>)
      -> tag_invoke_result_t<transform_done_cpo, Source, Done> {
    return tag_invoke(*this, (Source&&)source, (Done&&)done);
  }

  template<
    typename Source,
    typename Done,
    std::enable_if_t<
        !is_tag_invocable_v<transform_done_cpo, Source, Done> &&
        std::is_constructible_v<std::remove_cvref_t<Source>, Source> &&
        std::is_constructible_v<std::remove_cvref_t<Done>, Done> &&
        is_callable_v<std::remove_cvref_t<Done>>, int> = 0>
  auto operator()(Source&& source, Done&& done) const
      noexcept(std::is_nothrow_constructible_v<
                   transform_done_sender<Source, Done>,
                   Source,
                   Done>)
      -> transform_done_sender<Source, Done> {
    return transform_done_sender<Source, Done>{
        (Source&&)source, (Done&&)done};
  }
} transform_done{};

} // namespace unifex
