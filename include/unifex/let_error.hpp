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
#include <unifex/tag_invoke.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/bind_back.hpp>

#include <utility>
#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _let_e {
template <typename Source, typename Error, typename Receiver>
struct _op {
  class type;
};
template <typename Source, typename Error, typename Receiver>
using operation_type = typename _op<Source, Error, remove_cvref_t<Receiver>>::type;

template <typename Source, typename Error, typename Receiver>
struct _rcvr {
  class type;
};
template <typename Source, typename Error, typename Receiver>
using receiver_type = typename _rcvr<Source, Error, remove_cvref_t<Receiver>>::type;

template <typename Source, typename Error, typename Receiver>
struct _frcvr {
  class type;
};
template <typename Source, typename Error, typename Receiver>
using final_receiver_type = typename _frcvr<Source, Error, remove_cvref_t<Receiver>>::type;

template <typename Source, typename Error>
struct _sndr {
  class type;
};

template <typename Source, typename Error>
using _sender = typename _sndr<Source, Error>::type;

enum class state { neither, source, final };

template <typename Source, typename Error, typename Receiver>
class _rcvr<Source, Error, Receiver>::type final {
  using operation = operation_type<Source, Error, Receiver>;
  using final_receiver = final_receiver_type<Source, Error, Receiver>;
  using final_sender_t = callable_result_t<Error&>;

public:
  explicit type(operation* op) noexcept
  : op_(op) {}

  type(type&& other) noexcept
  : op_(std::exchange(other.op_, {}))
  {}
 
  template(typename... Values)
    (requires receiver_of<Receiver, Values...>)
  void set_value(Values&&... values) noexcept(
      is_nothrow_receiver_of_v<Receiver, Values...>) {
    UNIFEX_ASSERT(op_ != nullptr);
    unifex::set_value(std::move(op_->receiver_), (Values&&)values...);
  }

  void set_done() noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    unifex::set_done(std::move(op_->receiver_));
  }

  void set_error(_ignore) noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    auto op = op_; // preserve pointer value.
    if constexpr (
      is_nothrow_callable_v<Error> &&
      is_nothrow_connectable_v<final_sender_t, final_receiver>) {
      op->startedOp_ = state::neither;
      unifex::deactivate_union_member(op->sourceOp_);
      unifex::activate_union_member_with(op->finalOp_, [&] {
        return unifex::connect(std::move(op->error_)(), final_receiver{op});
      });
      op->startedOp_ = state::final;
      unifex::start(op->finalOp_.get());
    } else {
      UNIFEX_TRY {
        op->startedOp_ = state::neither;
        unifex::deactivate_union_member(op->sourceOp_);
        unifex::activate_union_member_with(op->finalOp_, [&] {
          return unifex::connect(std::move(op->error_)(), final_receiver{op});
        });
        op->startedOp_ = state::final;
        unifex::start(op->finalOp_.get());
      } UNIFEX_CATCH (...) {
        unifex::set_error(std::move(op->receiver_), std::current_exception());
      }
    }
  }

private:
  template(typename CPO, typename Self)
    (requires is_receiver_query_cpo_v<CPO> AND
        same_as<remove_cvref_t<Self>, type> AND
        is_callable_v<CPO, const Receiver&>)
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
    UNIFEX_ASSERT(op_ != nullptr);   
    return op_->receiver_;
  }

  operation* op_;
};

template <typename Source, typename Error, typename Receiver>
class _frcvr<Source, Error, Receiver>::type {
  using operation = operation_type<Source, Error, Receiver>;

public:
  explicit type(operation* op) noexcept
  : op_(op) {}

  type(type&& other) noexcept
  : op_(std::exchange(other.op_, {}))
  {}
 
  template(typename... Values)
    (requires receiver_of<Receiver, Values...>)
  void set_value(Values&&... values) noexcept(
      is_nothrow_receiver_of_v<Receiver, Values...>) {
    UNIFEX_ASSERT(op_ != nullptr);
    unifex::set_value(std::move(op_->receiver_), (Values&&)values...);
  }

  void set_done() noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    unifex::set_done(std::move(op_->receiver_));
  }

  template(typename Error2)
    (requires receiver<Receiver, Error2>)
  void set_error(Error2&& error2) noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    unifex::set_error(std::move(op_->receiver_), (Error2&&)error2);
  }

private:
  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO> AND
          is_callable_v<CPO, const Receiver&>)
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
    UNIFEX_ASSERT(op_ != nullptr);   
    return op_->receiver_;
  }

  operation* op_;
};

template <typename Source, typename Error, typename Receiver>
class _op<Source, Error, Receiver>::type {
  using source_receiver = receiver_type<Source, Error, Receiver>;
  using final_receiver = final_receiver_type<Source, Error, Receiver>;

public:
  template <typename Error2, typename Receiver2>
  explicit type(Source&& source, Error2&& error, Receiver2&& dest)
      noexcept(std::is_nothrow_move_constructible_v<Receiver> &&
               std::is_nothrow_move_constructible_v<Error> &&
               is_nothrow_connectable_v<Source, source_receiver>)
  : error_((Error2&&)error)
  , receiver_((Receiver2&&)dest)
  {
    unifex::activate_union_member_with(sourceOp_, [&] {
        return unifex::connect((Source&&)source, source_receiver{this});
      });
    startedOp_ = state::source;
  }

  ~type() {
    switch (startedOp_) {
    case state::neither:
      break;
    case state::source:
      unifex::deactivate_union_member(sourceOp_);
      break;
    case state::final:
      unifex::deactivate_union_member(finalOp_);
      break;
    }
    startedOp_ = state::neither;
  }

  void start() & noexcept {
    unifex::start(sourceOp_.get());
  }

private:
  friend source_receiver;
  friend final_receiver;

  using source_op_t = connect_result_t<Source, source_receiver>;

  using final_sender_t = callable_result_t<Error>;

  using final_op_t = connect_result_t<final_sender_t, final_receiver>;

  UNIFEX_NO_UNIQUE_ADDRESS Error error_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  state startedOp_ = state::neither;
  union {
    manual_lifetime<source_op_t> sourceOp_;
    manual_lifetime<final_op_t> finalOp_;
  };
};

template <typename Source, typename Error>
class _sndr<Source, Error>::type {
  using final_sender_t = callable_result_t<Error>;

public:
  template <template <typename...> class Variant,
            template <typename...> class Tuple>
  using value_types =
      typename concat_type_lists_unique_t<
          sender_value_types_t<Source, type_list, Tuple>,
          sender_value_types_t<final_sender_t, type_list, Tuple>>::template
              apply<Variant>;

  template <template <typename...> class Variant>
  using error_types =
      typename concat_type_lists_unique_t<
          sender_error_types_t<Source, type_list>,
          sender_error_types_t<final_sender_t, type_list>,
          type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<final_sender_t>::sends_done;

  template <typename Source2, typename Error2>
  explicit type(Source2&& source, Error2&& error)
    noexcept(
      std::is_nothrow_constructible_v<Source, Source2> &&
      std::is_nothrow_constructible_v<Error, Error2>)
    : source_((Source2&&)source)
    , error_((Error2&&)error)
  {}

  template(
    typename Sender,
    typename Receiver,
    typename...,
    typename SourceReceiver = receiver_type<member_t<Sender, Source>, Error, Receiver>,
    typename FinalReceiver = final_receiver_type<member_t<Sender, Source>, Error, Receiver>)
      (requires same_as<remove_cvref_t<Sender>, type> AND
          constructible_from<Error, member_t<Sender, Error>> AND
          constructible_from<remove_cvref_t<Receiver>, Receiver> AND
          sender_to<member_t<Sender, Source>, SourceReceiver> AND
          sender_to<final_sender_t, FinalReceiver>)
  friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r)
       noexcept(
        is_nothrow_connectable_v<member_t<Sender, Source>, SourceReceiver> &&
        std::is_nothrow_constructible_v<Error, member_t<Sender, Error>> &&
        std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>)
      -> operation_type<member_t<Sender, Source>, Error, Receiver> {
    return operation_type<member_t<Sender, Source>, Error, Receiver>{
      static_cast<Sender&&>(s).source_,
      static_cast<Sender&&>(s).error_,
      static_cast<Receiver&&>(r)
    };
  }

private:
  Source source_;
  Error error_;
};

namespace _cpo
{
struct _fn {
  template(typename Source, typename Error)
    (requires tag_invocable<_fn, Source, Error> AND
        sender<Source> AND
        callable<remove_cvref_t<Error>> AND
        sender<callable_result_t<remove_cvref_t<Error>>>)
  auto operator()(Source&& source, Error&& error) const
      noexcept(is_nothrow_tag_invocable_v<_fn, Source, Error>)
      -> tag_invoke_result_t<_fn, Source, Error> {
    return tag_invoke(*this, (Source&&)source, (Error&&)error);
  }

  template(typename Source, typename Error)
    (requires (!tag_invocable<_fn, Source, Error>) AND
        constructible_from<remove_cvref_t<Source>, Source> AND
        constructible_from<remove_cvref_t<Error>, Error> AND
        callable<remove_cvref_t<Error>> AND
        sender<callable_result_t<remove_cvref_t<Error>>>)
  auto operator()(Source&& source, Error&& error) const
      noexcept(std::is_nothrow_constructible_v<
                   _sender<remove_cvref_t<Source>, remove_cvref_t<Error>>,
                   Source, 
                   Error>)
      -> _sender<remove_cvref_t<Source>, remove_cvref_t<Error>> {
    return _sender<remove_cvref_t<Source>, remove_cvref_t<Error>>{
        (Source&&)source, (Error&&)error};
  }
  template(typename Error)
      (requires callable<remove_cvref_t<Error>> AND
        sender<callable_result_t<remove_cvref_t<Error>>>)
  constexpr auto operator()(Error&& error) const
      noexcept(is_nothrow_callable_v<
        tag_t<bind_back>, _fn, Error>)
      -> bind_back_result_t<_fn, Error> {
    return bind_back(*this, (Error&&)error);
  }
};
} // namespace _cpo
} // namespace _let_e

inline constexpr _let_e::_cpo::_fn let_error {};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
