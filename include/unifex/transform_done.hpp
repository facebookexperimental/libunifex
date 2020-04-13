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
  void set_value(Values&&... values)
    noexcept(is_nothrow_callable_v<tag_t<unifex::set_value>&, Receiver, Values...>) {
    assert(op_ != nullptr);
    unifex::set_value(std::move(op_->receiver_), (Values&&)values...);
  }

  UNIFEX_TEMPLATE(typename R = Receiver)
    (requires receiver<R>)
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

  UNIFEX_TEMPLATE(typename Error)
    (requires receiver<Receiver, Error>)
  void set_error(Error&& error) noexcept {
    assert(op_ != nullptr);
    unifex::set_error(std::move(op_->receiver_), (Error&&)error);
  }

private:
  UNIFEX_TEMPLATE(typename CPO, typename... Args)
    (requires (!is_receiver_cpo_v<CPO>))
  friend auto tag_invoke(CPO cpo, const type& r, Args&&... args)
      noexcept(is_nothrow_callable_v<CPO, const Receiver&, Args...>)
      -> callable_result_t<CPO, const Receiver&, Args...> {
    return std::move(cpo)(r.get_receiver(), (Args&&)args...);
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
 
  UNIFEX_TEMPLATE(typename... Values)
    (requires receiver_of<Receiver, Values...>)
  void set_value(Values&&... values) noexcept(is_nothrow_callable_v<tag_t<unifex::set_value>&, Receiver, Values...>) {
    assert(op_ != nullptr);
    unifex::set_value(std::move(op_->receiver_));
  }

  UNIFEX_TEMPLATE(typename R = Receiver)
    (requires receiver<R>)
  void set_done() noexcept {
    assert(op_ != nullptr);
    unifex::set_done(std::move(op_->receiver_));
  }

  UNIFEX_TEMPLATE(typename Error)
    (requires receiver<Receiver, Error>)
  void set_error(Error&& error) noexcept {
    assert(op_ != nullptr);
    unifex::set_error(std::move(op_->receiver_), (Error&&)error);
  }

private:
  template<typename CPO, typename... Args>
  friend auto tag_invoke(CPO cpo, const type& r, Args&&... args)
      noexcept(is_nothrow_callable_v<CPO, const Receiver&, Args...>)
      -> callable_result_t<CPO, const Receiver&, Args...> {
    return std::move(cpo)(r.get_receiver(), (Args&&)args...);
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

  using source_op_t = connect_result_t<Source, source_receiver>;

  using final_sender_t = callable_result_t<Done>;

  using final_op_t = connect_result_t<final_sender_t, final_receiver>;

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

  UNIFEX_TEMPLATE(
    typename Receiver,
    typename SourceReceiver = receiver_type<Source, Done, Receiver>,
    typename FinalReceiver = final_receiver_type<Source, Done, Receiver>)
    (requires move_constructible<Done> &&
        receiver<Receiver> &&
        sender_to<Source, SourceReceiver> &&
        sender_to<final_sender_t, FinalReceiver>)
  operation_type<Source, Done, Receiver> connect(Receiver&& r) &&
       noexcept(
        is_nothrow_connectable_v<Source, SourceReceiver> &&
        std::is_nothrow_move_constructible_v<Done> &&
        std::is_nothrow_constructible_v<std::remove_cvref_t<Receiver>, Receiver>) {
    return operation_type<Source, Done, Receiver>{
      (Source&&)source_, 
      (Done&&)done_, 
      (Receiver&&)r
    };
  }

  UNIFEX_TEMPLATE(
    typename Receiver,
    typename SourceReceiver = receiver_type<const Source&, Done, Receiver>,
    typename FinalReceiver = final_receiver_type<const Source&, Done, Receiver>)
    (requires copy_constructible<Done> &&
        receiver<Receiver> &&
        sender_to<const Source&, SourceReceiver> &&
        sender_to<final_sender_t, FinalReceiver>)
  operation_type<const Source&, Done, Receiver> connect(Receiver&& r) const&
       noexcept(
        is_nothrow_connectable_v<const Source&, SourceReceiver> &&
        std::is_nothrow_copy_constructible_v<Done> &&
        std::is_nothrow_constructible_v<std::remove_cvref_t<Receiver>, Receiver>) {
    return operation_type<const Source&, Done, Receiver>{
      source_, 
      done_, 
      (Receiver&&)r
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

  UNIFEX_TEMPLATE(typename Source, typename Done)
    (requires (!is_tag_invocable_v<transform_done_cpo, Source, Done>) &&
        constructible_from<std::remove_cvref_t<Source>, Source> &&
        constructible_from<std::remove_cvref_t<Done>, Done> &&
        callable<std::remove_cvref_t<Done>>)
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
