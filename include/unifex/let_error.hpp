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
#include <unifex/manual_lifetime_union.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/bind_back.hpp>

#include <utility>
#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _let_e {
template <typename Source, typename Func, typename Receiver>
struct _op final {
  class type;
};
template <typename Source, typename Func, typename Receiver>
using operation_type = typename _op<Source, Func, remove_cvref_t<Receiver>>::type;

template <typename Source, typename Func, typename Receiver>
struct _rcvr final {
  class type;
};
template <typename Source, typename Func, typename Receiver>
using receiver_type = typename _rcvr<Source, Func, remove_cvref_t<Receiver>>::type;

template <typename Source, typename Func, typename Receiver, typename Error>
struct _frcvr final {
  class type;
};
template <typename Source, typename Func, typename Receiver, typename Error>
using final_receiver_type =
    typename _frcvr<Source, Func, remove_cvref_t<Receiver>, Error>::type;

template <typename Source, typename Func>
struct _sndr final {
  class type;
};

template <typename Source, typename Func>
using _sender = typename _sndr<Source, Func>::type;

template <typename Source, typename Func, typename Receiver>
class _rcvr<Source, Func, Receiver>::type final {
  using operation = operation_type<Source, Func, Receiver>;
  template <typename Error>
  using final_receiver = final_receiver_type<Source, Func, Receiver, Error>;

public:
  explicit type(operation* op) noexcept : op_(op) {}
  type(type&& other) noexcept : op_(std::exchange(other.op_, {})) {}

  // Taking by value here to force a move/copy on the offchance the value
  // objects live in the operation state, in which case destroying the
  // predecessor operation state would invalidate it.
  //
  // Flipping the order of `deactivate_*` and `set_value` is UB since by the
  // time `set_value()` returns `op_` might as well be already destroyed,
  // without proper deactivation of `op_->sourceOp_`.
  template(typename... Values)
    (requires receiver_of<Receiver, Values...>)
  void set_value(Values... values) noexcept(
      is_nothrow_receiver_of_v<Receiver, Values...>) {
    // local copy, b/c deactivate_union_member deletes this
    auto op = op_;
    UNIFEX_ASSERT(op != nullptr);
    unifex::deactivate_union_member(op->sourceOp_);
    unifex::set_value(std::move(op->receiver_), std::move(values)...);
  }

  void set_done() noexcept {
    // local copy, b/c deactivate_union_member deletes this
    auto op = op_;
    UNIFEX_ASSERT(op != nullptr);
    unifex::deactivate_union_member(op->sourceOp_);
    unifex::set_done(std::move(op->receiver_));
  }

  template <typename ErrorValue>
  void set_error(ErrorValue&& e) noexcept {
    // local copy, b/c deactivate_union_member deletes this
    auto op = op_;
    UNIFEX_ASSERT(op != nullptr);

    using final_sender_t = callable_result_t<Func, remove_cvref_t<ErrorValue>&>;
    using final_op_t = connect_result_t<
        final_sender_t,
        final_receiver<remove_cvref_t<ErrorValue>>>;

    UNIFEX_TRY {
      scope_guard destroyPredOp = [&]() noexcept {
        unifex::deactivate_union_member(op->sourceOp_);
      };
      auto& err = op->error_.template construct<remove_cvref_t<ErrorValue>>(
          (ErrorValue &&) e);
      destroyPredOp.reset();
      scope_guard destroyErr = [&]() noexcept {
        op->error_.template destruct<remove_cvref_t<ErrorValue>>();
      };
      auto& finalOp =
          unifex::activate_union_member_with<final_op_t>(op->finalOp_, [&] {
            return unifex::connect(
                std::move(op->func_)(err),
                final_receiver<remove_cvref_t<ErrorValue>>{op});
          });
      unifex::start(finalOp);
      destroyErr.release();
    }
    UNIFEX_CATCH(...) {
      unifex::set_error(std::move(op->receiver_), std::current_exception());
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

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      VisitFunc&& func) noexcept(is_nothrow_callable_v<
                                VisitFunc&,
                                const Receiver&>) {
    func(r.get_receiver());
  }
#endif

  const Receiver& get_receiver() const noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    return op_->receiver_;
  }

  operation* op_;
};

template <typename Source, typename Func, typename Receiver, typename Error>
class _frcvr<Source, Func, Receiver, Error>::type final {
  using operation = operation_type<Source, Func, Receiver>;
  static_assert(!std::is_reference_v<Error>);

public:
  explicit type(operation* op) noexcept : op_(op) {}
  type(type&& other) noexcept : op_(std::exchange(other.op_, {})) {}

  template(typename... Values)
    (requires receiver_of<Receiver, Values...>)
  // Taking by value here to force a move/copy on the offchance the value
  // objects live in the operation state, in which case destroying the
  // predecessor operation state would invalidate it.
  void set_value(Values... values) noexcept(
      is_nothrow_receiver_of_v<Receiver, Values...>) {
    // local copy, b/c cleanup deletes this
    auto op = op_;
    cleanup(op);
    UNIFEX_TRY {
      unifex::set_value(std::move(op->receiver_), std::move(values)...);
    } UNIFEX_CATCH (...) {
      unifex::set_error(std::move(op->receiver_), std::current_exception());
    }
  }

  void set_done() noexcept {
    // local copy, b/c cleanup deletes this
    auto op = op_;
    cleanup(op);
    unifex::set_done(std::move(op->receiver_));
  }

  template(typename ErrorValue)
    (requires receiver<Receiver, ErrorValue>)
  // Taking by value here to force a copy on the offchance the error
  // object lives in the operation state, in which
  // case the call to cleanup() would invalidate them.
  void set_error(ErrorValue error) noexcept {
    // local copy, b/c cleanup deletes this
    auto op = op_;
    cleanup(op);
    unifex::set_error(std::move(op->receiver_), std::move(error));
  }

private:
  static void cleanup(operation* op) noexcept {
    using final_sender_t = callable_result_t<Func, Error&>;
    using final_op_t = connect_result_t<final_sender_t, type>;
    UNIFEX_ASSERT(op != nullptr);
    unifex::deactivate_union_member<final_op_t>(op->finalOp_);
    op->error_.template destruct<Error>();
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO> AND
          is_callable_v<CPO, const Receiver&>)
  friend auto tag_invoke(CPO cpo, const type& r)
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(r.get_receiver());
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      VisitFunc&& func) noexcept(is_nothrow_callable_v<
                                VisitFunc&,
                                const Receiver&>) {
    func(r.get_receiver());
  }
#endif

  const Receiver& get_receiver() const noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    return op_->receiver_;
  }

  operation* op_;
};

template <typename Source, typename Func, typename Receiver>
class _op<Source, Func, Receiver>::type final {
  using source_receiver = receiver_type<Source, Func, Receiver>;
  template <typename Error>
  using final_receiver = final_receiver_type<Source, Func, Receiver, Error>;

public:
  template <typename Func2, typename Receiver2>
  explicit type(Source&& source, Func2&& func, Receiver2&& dest) noexcept(
      std::is_nothrow_constructible_v<Receiver, Receiver2&&>&&
          std::is_nothrow_constructible_v<Func, Func2&&>&&
              is_nothrow_connectable_v<Source, source_receiver>)
    : func_((Func2 &&) func)
    , receiver_((Receiver2 &&) dest) {
    unifex::activate_union_member_with(sourceOp_, [&] {
      return unifex::connect((Source &&) source, source_receiver{this});
    });
  }

  ~type() {
    if (!started_) {
      unifex::deactivate_union_member(sourceOp_);
    }
  }

  void start() & noexcept {
    started_ = true;
    unifex::start(sourceOp_.get());
  }

private:
  friend source_receiver;
  template <
      typename Source2,
      typename Func2,
      typename Receiver2,
      typename Error2>
  friend struct _frcvr;

  using source_type = remove_cvref_t<Source>;

  using source_op_t = connect_result_t<Source, source_receiver>;

  template <typename Error>
  using final_sender_t = callable_result_t<Func, remove_cvref_t<Error>&>;

  template <typename Error>
  using final_receiver_t =
      final_receiver_type<Source, Func, Receiver, remove_cvref_t<Error>>;

  template <typename Error>
  using final_op_t =
      connect_result_t<final_sender_t<Error>, final_receiver_t<Error>>;

  template <typename... Errors>
  using final_op_union = manual_lifetime_union<final_op_t<Errors>...>;

  using final_op_union_t = sender_error_types_t<source_type, final_op_union>;

  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  UNIFEX_NO_UNIQUE_ADDRESS
      sender_error_types_t<source_type, manual_lifetime_union>
          error_;
  union {
    manual_lifetime<source_op_t> sourceOp_;
    final_op_union_t finalOp_;
  };
  bool started_ = false;
};

template<typename Sender>
struct sends_done_impl : std::bool_constant<sender_traits<Sender>::sends_done> {};

template<typename... Senders>
using any_sends_done = std::disjunction<sends_done_impl<Senders>...>;

template <typename Source, typename Func>
class _sndr<Source, Func>::type final {

  template <typename Error>
  using final_sender = callable_result_t<Func, remove_cvref_t<Error>&>;

  using final_senders_list =
      map_type_list_t<sender_error_type_list_t<Source>, final_sender>;

  template <typename... Errors>
  using sends_done_impl = any_sends_done<Source, final_sender<Errors>...>;

public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = type_list_nested_apply_t<
      concat_type_lists_unique_t<
          sender_value_type_list_t<Source>,
          apply_to_type_list_t<
              concat_type_lists_unique_t,
              map_type_list_t<final_senders_list, sender_value_type_list_t>>>,
      Variant,
      Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_type_list_t<Source>,
      apply_to_type_list_t<
          concat_type_lists_unique_t,
          map_type_list_t<final_senders_list, sender_error_type_list_t>>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done =
    sender_traits<Source>::sends_done ||
    sender_error_types_t<Source, sends_done_impl>::value;

  template <typename Source2, typename Func2>
  explicit type(Source2&& source, Func2&& func)
    noexcept(
      std::is_nothrow_constructible_v<Source, Source2> &&
      std::is_nothrow_constructible_v<Func, Func2>)
    : source_((Source2&&)source)
    , func_((Func2&&)func)
  {}

  template(
    typename Sender,
    typename Receiver,
    typename...,
    typename SourceReceiver = receiver_type<member_t<Sender, Source>, Func, Receiver>)
      (requires receiver<Receiver> AND
          same_as<remove_cvref_t<Sender>, type> AND
          constructible_from<Func, member_t<Sender, Func>> AND
          constructible_from<remove_cvref_t<Receiver>, Receiver> AND
          sender_to<Source, SourceReceiver>)
  friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r)
       noexcept(
        is_nothrow_connectable_v<member_t<Sender, Source>, SourceReceiver> &&
        std::is_nothrow_constructible_v<Func, member_t<Sender, Func>> &&
        std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>)
      -> operation_type<Source, Func, Receiver> {
    return operation_type<Source, Func, Receiver>{
      static_cast<Sender&&>(s).source_,
      static_cast<Sender&&>(s).func_,
      static_cast<Receiver&&>(r)
    };
  }

private:
  Source source_;
  Func func_;
};

namespace _cpo
{
struct _fn final {
  template(typename Source, typename Func)
    (requires tag_invocable<_fn, Source, Func> AND
        sender<Source>)
  auto operator()(Source&& source, Func&& func) const
      noexcept(is_nothrow_tag_invocable_v<_fn, Source, Func>)
      -> tag_invoke_result_t<_fn, Source, Func> {
    return tag_invoke(*this, (Source&&)source, (Func&&)func);
  }

  template(typename Source, typename Func)
    (requires (!tag_invocable<_fn, Source, Func>) AND
        constructible_from<remove_cvref_t<Source>, Source> AND
        constructible_from<remove_cvref_t<Func>, Func>)
  auto operator()(Source&& source, Func&& func) const
      noexcept(std::is_nothrow_constructible_v<
                   _sender<remove_cvref_t<Source>, remove_cvref_t<Func>>,
                   Source,
                   Func>)
      -> _sender<remove_cvref_t<Source>, remove_cvref_t<Func>> {
    return _sender<remove_cvref_t<Source>, remove_cvref_t<Func>>{
        (Source&&)source, (Func&&)func};
  }
  template <typename Func>
  constexpr auto operator()(Func&& func) const
      noexcept(is_nothrow_callable_v<
        tag_t<bind_back>, _fn, Func>)
      -> bind_back_result_t<_fn, Func> {
    return bind_back(*this, (Func&&)func);
  }
};
} // namespace _cpo
} // namespace _let_e

inline constexpr _let_e::_cpo::_fn let_error {};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
