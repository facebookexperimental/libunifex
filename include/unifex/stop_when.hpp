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
#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include <variant>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _stop_when {
template <typename Source, typename Trigger, typename Receiver>
struct _op {
  class type;
};
template <typename Source, typename Trigger, typename Receiver>
using stop_when_operation = typename _op<Source, Trigger, Receiver>::type;

template <typename Source, typename Trigger, typename Receiver>
struct _srcvr {
  class type;
};

template <typename Source, typename Trigger, typename Receiver>
using stop_when_source_receiver =
    typename _srcvr<Source, Trigger, Receiver>::type;

template <typename Source, typename Trigger, typename Receiver>
class _srcvr<Source, Trigger, Receiver>::type {
  using operation_state = stop_when_operation<Source, Trigger, Receiver>;

public:
  explicit type(operation_state* op) noexcept : op_(op) {}

  type(type&& other) noexcept : op_(std::exchange(other.op_, nullptr)) {}

  template <typename... Values>
  void set_value(Values&&... values) && {
    op_->result_.template emplace<
        std::tuple<tag_t<unifex::set_value>, std::decay_t<Values>...>>(
        unifex::set_value, (Values &&) values...);
    op_->notify_source_complete();
  }

  template <typename Error>
  void set_error(Error&& error) && noexcept {
    op_->result_.template emplace<
        std::tuple<tag_t<unifex::set_error>, std::decay_t<Error>>>(
        unifex::set_error, (Error &&) error);
    op_->notify_source_complete();
  }

  void set_done() && noexcept {
    op_->result_.template emplace<std::tuple<tag_t<unifex::set_done>>>(
        unifex::set_done);
    op_->notify_source_complete();
  }

private:
  friend inplace_stop_token
  tag_invoke(tag_t<unifex::get_stop_token>, const type& r) noexcept {
    return r.get_stop_token();
  }

  template(typename CPO)                       //
      (requires is_receiver_query_cpo_v<CPO>)  //
      friend auto tag_invoke(CPO cpo, const type& r) noexcept(
          is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(r.get_receiver());
  }

  inplace_stop_token get_stop_token() const noexcept {
    return op_->stopSource_.get_token();
  }

  const Receiver& get_receiver() const noexcept { return op_->receiver_; }

  operation_state* op_;
};

template <typename Source, typename Trigger, typename Receiver>
struct _trcvr {
  class type;
};

template <typename Source, typename Trigger, typename Receiver>
using stop_when_trigger_receiver =
    typename _trcvr<Source, Trigger, Receiver>::type;

template <typename Source, typename Trigger, typename Receiver>
class _trcvr<Source, Trigger, Receiver>::type {
  using operation_state = stop_when_operation<Source, Trigger, Receiver>;

public:
  explicit type(operation_state* op) noexcept : op_(op) {}

  type(type&& other) noexcept : op_(std::exchange(other.op_, nullptr)) {}

  void set_value() && noexcept { op_->notify_trigger_complete(); }

  template <typename Error>
  void set_error(Error&&) && noexcept {
    op_->notify_trigger_complete();
  }

  void set_done() && noexcept { op_->notify_trigger_complete(); }

private:
  friend inplace_stop_token
  tag_invoke(tag_t<unifex::get_stop_token>, const type& r) noexcept {
    return r.get_stop_token();
  }

  template(typename CPO)                       //
      (requires is_receiver_query_cpo_v<CPO>)  //
      friend auto tag_invoke(CPO cpo, const type& r) noexcept(
          is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(r.get_receiver());
  }

  inplace_stop_token get_stop_token() const noexcept {
    return op_->stopSource_.get_token();
  }

  const Receiver& get_receiver() const noexcept { return op_->receiver_; }

  operation_state* op_;
};

template <typename Source, typename Trigger, typename Receiver>
class _op<Source, Trigger, Receiver>::type {
  using source_receiver = stop_when_source_receiver<Source, Trigger, Receiver>;
  using trigger_receiver =
      stop_when_trigger_receiver<Source, Trigger, Receiver>;

public:
  template <typename Receiver2>
  explicit type(
      Source&& source,
      Trigger&& trigger,
      Receiver2&&
          receiver) noexcept(is_nothrow_connectable_v<Source, source_receiver>&&
                                 is_nothrow_connectable_v<
                                     Trigger,
                                     trigger_receiver>&&
                                     std::is_nothrow_constructible_v<
                                         Receiver,
                                         Receiver2>)
    : receiver_((Receiver2 &&) receiver)
    , sourceOp_(unifex::connect((Source &&) source, source_receiver{this}))
    , triggerOp_(
          unifex::connect((Trigger &&) trigger, trigger_receiver{this})) {}

  void start() & noexcept {
    stopCallback_.emplace(get_stop_token(receiver_), cancel_callback{this});

    unifex::start(sourceOp_);
    unifex::start(triggerOp_);
  }

private:
  friend class _srcvr<Source, Trigger, Receiver>::type;
  friend class _trcvr<Source, Trigger, Receiver>::type;

  class cancel_callback {
  public:
    explicit cancel_callback(type* op) noexcept : op_(op) {}

    void operator()() noexcept {
      // save this on the stack; it's likely this callback object will be
      // destroyed as a side effect of requesting stop on the operation's stop
      // source so we can't use this->op_ after request_stop() returns
      auto op = op_;

      if (op->activeOpCount_.fetch_add(1, std::memory_order_relaxed) == 0) {
        // someone's already invoked deliver_result() so we should bail out
        return;
      }

      op->stopSource_.request_stop();

      if (op->activeOpCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // we're the last owner of the operation so deliver its result now
        op->deliver_result();
      }
    }

  private:
    type* op_;
  };

  void notify_source_complete() noexcept { this->notify_trigger_complete(); }

  void notify_trigger_complete() noexcept {
    stopSource_.request_stop();
    if (activeOpCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      stopCallback_.reset();
      deliver_result();
    }
  }

  void deliver_result() noexcept {
    UNIFEX_TRY {
      std::visit(
          [this](auto&& tuple) {
            if constexpr (
                std::tuple_size<
                    std::remove_reference_t<decltype(tuple)>>::value != 0) {
              std::apply(
                  [&](auto set_xxx, auto&&... args) {
                    set_xxx(
                        std::move(receiver_),
                        static_cast<decltype(args)>(args)...);
                  },
                  static_cast<decltype(tuple)>(tuple));
            } else {
              // Should be unreachable
              std::terminate();
            }
          },
          std::move(result_));
    }
    UNIFEX_CATCH(...) {
      unifex::set_error(std::move(receiver_), std::current_exception());
    }
  }

  template <typename... Values>
  using value_decayed_tuple =
      std::tuple<tag_t<unifex::set_value>, std::decay_t<Values>...>;

  template <typename... Errors>
  using error_tuples =
      type_list<std::tuple<tag_t<unifex::set_error>, std::decay_t<Errors>>...>;

  using result_variant = typename concat_type_lists_t<
      type_list<std::tuple<>, std::tuple<tag_t<unifex::set_done>>>,
      sender_value_types_t<
          remove_cvref_t<Source>,
          type_list,
          value_decayed_tuple>,
      sender_error_types_t<remove_cvref_t<Source>, error_tuples>>::
      template apply<std::variant>;

  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  std::atomic<int> activeOpCount_ = 2;
  inplace_stop_source stopSource_;
  std::optional<typename stop_token_type_t<Receiver>::template callback_type<
      cancel_callback>>
      stopCallback_;
  UNIFEX_NO_UNIQUE_ADDRESS result_variant result_;
  UNIFEX_NO_UNIQUE_ADDRESS connect_result_t<Source, source_receiver> sourceOp_;
  UNIFEX_NO_UNIQUE_ADDRESS
  connect_result_t<Trigger, trigger_receiver> triggerOp_;
};

template <typename Source, typename Trigger>
struct _sndr {
  class type;
};

template <typename Source, typename Trigger>
using stop_when_sender = typename _sndr<Source, Trigger>::type;

template <typename Source, typename Trigger>
class _sndr<Source, Trigger>::type {
  using stop_when_sender = type;

  template <typename... Values>
  using decayed_type_list = type_list<type_list<std::decay_t<Values>...>>;

  template <
      template <typename...>
      class Outer,
      template <typename...>
      class Inner>
  struct compose_nested {
    template <typename... Lists>
    using apply = Outer<typename Lists::template apply<Inner>...>;
  };

public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = typename sender_traits<Source>::
      template value_types<concat_type_lists_unique_t, decayed_type_list>::
          template apply<compose_nested<Variant, Tuple>::template apply>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Source, decayed_tuple<type_list>::template apply>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = true;

  template <typename Source2, typename Trigger2>
  explicit type(Source2&& source, Trigger2&& trigger) noexcept(
      std::is_nothrow_constructible_v<Source, Source2>&&
          std::is_nothrow_constructible_v<Trigger, Trigger2>)
    : source_((Source2 &&) source)
    , trigger_((Trigger2 &&) trigger) {}

  template(typename Self, typename Receiver)  //
      (requires same_as<remove_cvref_t<Self>, type> AND receiver<Receiver> AND
           sender_to<
               member_t<Self, Source>,
               stop_when_source_receiver<
                   member_t<Self, Source>,
                   member_t<Self, Trigger>,
                   remove_cvref_t<Receiver>>> AND
               sender_to<
                   member_t<Self, Trigger>,
                   stop_when_trigger_receiver<
                       member_t<Self, Source>,
                       member_t<Self, Trigger>,
                       remove_cvref_t<Receiver>>>)  //
      friend auto tag_invoke(tag_t<connect>, Self&& self, Receiver&& r)
          -> stop_when_operation<
              member_t<Self, Source>,
              member_t<Self, Trigger>,
              remove_cvref_t<Receiver>> {
    return stop_when_operation<
        member_t<Self, Source>,
        member_t<Self, Trigger>,
        remove_cvref_t<Receiver>>{
        ((Self &&) self).source_, ((Self &&) self).trigger_, (Receiver &&) r};
  }

private:
  UNIFEX_NO_UNIQUE_ADDRESS Source source_;
  UNIFEX_NO_UNIQUE_ADDRESS Trigger trigger_;
};
}  // namespace _stop_when

namespace _stop_when_cpo {
struct _fn {
  template(typename Source, typename Trigger)         //
      (requires tag_invocable<_fn, Source, Trigger>)  //
      auto
      operator()(Source&& source, Trigger&& trigger) const
      noexcept(is_nothrow_tag_invocable_v<_fn, Source, Trigger>)
          -> tag_invoke_result_t<_fn, Source, Trigger> {
    return unifex::tag_invoke(*this, (Source &&) source, (Trigger &&) trigger);
  }

  template(typename Source, typename Trigger)           //
      (requires(!tag_invocable<_fn, Source, Trigger>))  //
      auto
      operator()(Source&& source, Trigger&& trigger) const noexcept(
          std::is_nothrow_constructible_v<remove_cvref_t<Source>, Source>&&
              std::is_nothrow_constructible_v<remove_cvref_t<Trigger>, Trigger>)
          -> _stop_when::stop_when_sender<
              remove_cvref_t<Source>,
              remove_cvref_t<Trigger>> {
    return _stop_when::
        stop_when_sender<remove_cvref_t<Source>, remove_cvref_t<Trigger>>(
            (Source &&) source, (Trigger &&) trigger);
  }
  template <typename Trigger>
  constexpr auto operator()(Trigger&& trigger) const
      noexcept(is_nothrow_callable_v<tag_t<bind_back>, _fn, Trigger>)
          -> bind_back_result_t<_fn, Trigger> {
    return bind_back(*this, (Trigger &&) trigger);
  }
};

}  // namespace _stop_when_cpo

inline constexpr _stop_when_cpo::_fn stop_when{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
