/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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
#include <unifex/async_trace.hpp>
#include <unifex/bind_back.hpp>

#include <utility>
#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _transform_done {
template <typename Source, typename Done, typename Receiver>
struct _op {
  class type;
};
template <typename Source, typename Done, typename Receiver>
using operation_type = typename _op<Source, Done, remove_cvref_t<Receiver>>::type;

template <typename Source, typename Done, typename Receiver>
struct _rcvr {
  class type;
};
template <typename Source, typename Done, typename Receiver>
using receiver_type = typename _rcvr<Source, Done, remove_cvref_t<Receiver>>::type;

template <typename Source, typename Done, typename Receiver>
struct _frcvr {
  class type;
};
template <typename Source, typename Done, typename Receiver>
using final_receiver_type = typename _frcvr<Source, Done, remove_cvref_t<Receiver>>::type;

template <typename Source, typename Done>
struct _sndr {
  class type;
};

template <typename Source, typename Done, typename Receiver>
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
 
  template(typename... Values)
    (requires receiver_of<Receiver, Values...>)
  void set_value(Values&&... values) noexcept(
      is_nothrow_receiver_of_v<Receiver, Values...>) {
    UNIFEX_ASSERT(op_ != nullptr);
    unifex::set_value(std::move(op_->receiver_), (Values&&)values...);
  }

  void set_done() noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    auto op = op_; // preserve pointer value.
    if constexpr (
      is_nothrow_callable_v<Done> &&
      is_nothrow_connectable_v<final_sender_t, final_receiver>) {
      op->startedOp_ = 0;
      unifex::deactivate_union_member(op->sourceOp_);
      unifex::activate_union_member_with(op->finalOp_, [&] {
        return unifex::connect(std::move(op->done_)(), final_receiver{op});
      });
      op->startedOp_ = 0 - 1;
      unifex::start(op->finalOp_.get());
    } else {
      UNIFEX_TRY {
        op->startedOp_ = 0;
        unifex::deactivate_union_member(op->sourceOp_);
        unifex::activate_union_member_with(op->finalOp_, [&] {
          return unifex::connect(std::move(op->done_)(), final_receiver{op});
        });
        op->startedOp_ = 0 - 1;
        unifex::start(op->finalOp_.get());
      } UNIFEX_CATCH (...) {
        unifex::set_error(std::move(op->receiver_), std::current_exception());
      }
    }
  }

  template(typename Error)
      (requires receiver<Receiver, Error>)
  void set_error(Error&& error) noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    unifex::set_error(std::move(op_->receiver_), (Error&&)error);
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

template <typename Source, typename Done, typename Receiver>
class _frcvr<Source, Done, Receiver>::type {
  using operation = operation_type<Source, Done, Receiver>;

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

  template(typename Error)
    (requires receiver<Receiver, Error>)
  void set_error(Error&& error) noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    unifex::set_error(std::move(op_->receiver_), (Error&&)error);
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

template <typename Source, typename Done, typename Receiver>
class _op<Source, Done, Receiver>::type {
  using source_receiver = receiver_type<Source, Done, Receiver>;
  using final_receiver = final_receiver_type<Source, Done, Receiver>;

public:
  template <typename Done2, typename Receiver2>
  explicit type(Source&& source, Done2&& done, Receiver2&& dest)
      noexcept(is_nothrow_move_constructible_v<Receiver> &&
               is_nothrow_move_constructible_v<Done> &&
               is_nothrow_connectable_v<Source, source_receiver>)
  : done_((Done2&&)done)
  , receiver_((Receiver2&&)dest)
  {
    unifex::activate_union_member_with(sourceOp_, [&] {
        return unifex::connect((Source&&)source, source_receiver{this});
      });
    startedOp_ = 0 + 1;
  }

  ~type() {
    if (startedOp_ < 0) {
      unifex::deactivate_union_member(finalOp_);
    } else if (startedOp_ > 0) {
      unifex::deactivate_union_member(sourceOp_);
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

template <typename Source, typename Done>
class _sndr<Source, Done>::type {
  using final_sender_t = callable_result_t<Done>;

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

  template <typename Source2, typename Done2>
  explicit type(Source2&& source, Done2&& done)
    noexcept(
      is_nothrow_constructible_v<Source, Source2> &&
      is_nothrow_constructible_v<Done, Done2>)
    : source_((Source2&&)source)
    , done_((Done2&&)done)
  {}

  template(
    typename Sender,
    typename Receiver,
    typename SourceReceiver = receiver_type<member_t<Sender, Source>, Done, Receiver>,
    typename FinalReceiver = final_receiver_type<member_t<Sender, Source>, Done, Receiver>)
      (requires same_as<remove_cvref_t<Sender>, type> AND
          constructible_from<Done, member_t<Sender, Done>> AND
          constructible_from<remove_cvref_t<Receiver>, Receiver> AND
          sender_to<member_t<Sender, Source>, SourceReceiver> AND
          sender_to<final_sender_t, FinalReceiver>)
  friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r)
       noexcept(
        is_nothrow_connectable_v<member_t<Sender, Source>, SourceReceiver> &&
        is_nothrow_constructible_v<Done, member_t<Sender, Done>> &&
        is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>)
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

template <class Source, class Done>
using transform_done_sender =
    typename _transform_done::_sndr<remove_cvref_t<Source>, remove_cvref_t<Done>>::type;

inline const struct transform_done_cpo {
  template <typename Source, typename Done>
  auto operator()(Source&& source, Done&& done) const
      noexcept(is_nothrow_tag_invocable_v<transform_done_cpo, Source, Done>)
      -> tag_invoke_result_t<transform_done_cpo, Source, Done> {
    return tag_invoke(*this, (Source&&)source, (Done&&)done);
  }

  template(typename Source, typename Done)
    (requires (!tag_invocable<transform_done_cpo, Source, Done>) AND
        constructible_from<remove_cvref_t<Source>, Source> AND
        constructible_from<remove_cvref_t<Done>, Done> AND
        callable<remove_cvref_t<Done>>)
  auto operator()(Source&& source, Done&& done) const
      noexcept(is_nothrow_constructible_v<
                   transform_done_sender<Source, Done>,
                   Source, 
                   Done>)
      -> transform_done_sender<Source, Done> {
    return transform_done_sender<Source, Done>{
        (Source&&)source, (Done&&)done};
  }
  template(typename Done)
      (requires callable<remove_cvref_t<Done>>)
  constexpr auto operator()(Done&& done) const
      noexcept(is_nothrow_callable_v<
        tag_t<bind_back>, transform_done_cpo, Done>)
      -> bind_back_result_t<transform_done_cpo, Done> {
    return bind_back(*this, (Done&&)done);
  }
} transform_done{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
