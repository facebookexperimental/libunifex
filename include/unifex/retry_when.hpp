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

namespace detail {

template<typename Source, typename Func, typename Receiver>
class retry_when_operation;

template<typename Source, typename Func, typename Receiver>
class retry_when_source_receiver;

template<typename Source, typename Func, typename Receiver, typename Trigger>
class retry_when_trigger_receiver {
  using operation = retry_when_operation<Source, Func, Receiver>;

public:
  explicit retry_when_trigger_receiver(operation* op) noexcept
  : op_(op) {}

  retry_when_trigger_receiver(retry_when_trigger_receiver&& other) noexcept
  : op_(std::exchange(other.op_, nullptr))
  {}

  void set_value() && noexcept {
    assert(op_ != nullptr);   

    // This signals to retry the operation.
    auto* op = op_;
    destroy_trigger_op();

    using source_receiver = retry_when_source_receiver<Source, Func, Receiver>;

    if constexpr (is_nothrow_connectable_v<Source&, source_receiver>) {
      auto& sourceOp = op->sourceOp_.construct_from([&]() noexcept {
          return unifex::connect(op->source_, source_receiver{op});
        });
      op->isSourceOpConstructed_ = true;
      unifex::start(sourceOp);
    } else {
      try {
        auto& sourceOp = op->sourceOp_.construct_from([&] {
            return unifex::connect(op->source_, source_receiver{op});
          });
        op->isSourceOpConstructed_ = true;
        unifex::start(sourceOp);
      } catch (...) {
        unifex::set_error((Receiver&&)op->receiver_, std::current_exception());
      }
    }
  }

  template<
    typename R = Receiver,
    std::enable_if_t<std::is_invocable_v<decltype(unifex::set_done), Receiver>, int> = 0>
  void set_done() && noexcept {
    assert(op_ != nullptr);   

    auto* op = op_;
    destroy_trigger_op();
    unifex::set_done((Receiver&&)op->receiver_);
  }

  template<
    typename Error,
    std::enable_if_t<std::is_invocable_v<decltype(unifex::set_error), Receiver, Error>, int> = 0>
  void set_error(Error error) && noexcept {
    assert(op_ != nullptr);   

    auto* op = op_;

    // Note, parameter taken by value so that its lifetime will continue
    // to be valid after we destroy the operation-state that sent it.
    destroy_trigger_op();

    unifex::set_error((Receiver&&)op->receiver_, (Error&&)error);
  }

private:

  template<typename CPO, typename... Args>
  friend auto tag_invoke(CPO cpo, const retry_when_trigger_receiver& r, Args&&... args)
      noexcept(std::is_nothrow_invocable_v<CPO, const Receiver&, Args...>)
      -> std::invoke_result_t<CPO, const Receiver&, Args...> {
    return std::move(cpo)(r.get_receiver(), (Args&&)args...);
  }

  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const retry_when_trigger_receiver& r,
      VisitFunc&& func) noexcept(std::is_nothrow_invocable_v<
                                VisitFunc&,
                                const Receiver&>) {
    std::invoke(func, r.get_receiver());
  }
  
  const Receiver& get_receiver() const noexcept {
    assert(op_ != nullptr);
    return op_->receiver_;
  }

  void destroy_trigger_op() noexcept {
    using trigger_op = operation_t<Trigger, retry_when_trigger_receiver>;
    op_->triggerOps_.template get<trigger_op>().destruct();
  }

  operation* op_;
};

template<typename Source, typename Func, typename Receiver>
class retry_when_source_receiver {
  using operation = retry_when_operation<Source, Func, Receiver>;
public:
  explicit retry_when_source_receiver(operation* op) noexcept
  : op_(op) {}

  retry_when_source_receiver(retry_when_source_receiver&& other) noexcept
  : op_(std::exchange(other.op_, {}))
  {}
 
  template<
    typename... Values,
    std::enable_if_t<std::is_invocable_v<decltype(unifex::set_value), Receiver, Values...>, int> = 0>
  void set_value(Values&&... values)
      noexcept(std::is_nothrow_invocable_v<decltype(unifex::set_value), Receiver, Values...>) {
    assert(op_ != nullptr);
    unifex::set_value(std::move(op_->receiver_), (Values&&)values...);
  }

  template<
    typename R = Receiver,
    std::enable_if_t<std::is_invocable_v<decltype(unifex::set_done), R>, int> = 0>
  void set_done() noexcept {
    assert(op_ != nullptr);
    unifex::set_done(std::move(op_->receiver_));
  }

  template<
    typename Error,
    std::enable_if_t<std::is_invocable_v<Func&, Error>, int> = 0>
  void set_error(Error error) noexcept {
    assert(op_ != nullptr);
    auto* op = op_;

    op->isSourceOpConstructed_ = false;
    op->sourceOp_.destruct();

    using trigger_sender = std::invoke_result_t<Func&, Error>;
    using trigger_receiver = detail::retry_when_trigger_receiver<Source, Func, Receiver, trigger_sender>;
    using trigger_op = unifex::operation_t<trigger_sender, trigger_receiver>; 
    auto& triggerOpStorage = op->triggerOps_.template get<trigger_op>();
    if constexpr (std::is_nothrow_invocable_v<Func&, Error> &&
                  is_nothrow_connectable_v<trigger_sender, trigger_receiver>) {
      auto& triggerOp = triggerOpStorage.construct_from([&]() noexcept {
          return unifex::connect(std::invoke(op->func_, (Error&&)error), trigger_receiver{op});
        });
      unifex::start(triggerOp);
    } else {
      try {
        auto& triggerOp = triggerOpStorage.construct_from([&]() {
            return unifex::connect(std::invoke(op->func_, (Error&&)error), trigger_receiver{op});
          });
        unifex::start(triggerOp);
      } catch (...) {
        unifex::set_error((Receiver&&)op->receiver_, std::current_exception());
      }
    }
  }

private:
  template<typename CPO, typename... Args>
  friend auto tag_invoke(CPO cpo, const retry_when_source_receiver& r, Args&&... args)
      noexcept(std::is_nothrow_invocable_v<CPO, const Receiver&, Args...>)
      -> std::invoke_result_t<CPO, const Receiver&, Args...> {
    return std::move(cpo)(r.get_receiver(), (Args&&)args...);
  }
  
  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const retry_when_source_receiver& r,
      VisitFunc&& func) noexcept(std::is_nothrow_invocable_v<
                                VisitFunc&,
                                const Receiver&>) {
    std::invoke(func, r.get_receiver());
  }

  const Receiver& get_receiver() const noexcept {
    assert(op_ != nullptr);   
    return op_->receiver_;
  }

  operation* op_;
};

template<typename Source, typename Func, typename Receiver>
class retry_when_operation {
  using source_receiver = detail::retry_when_source_receiver<Source, Func, Receiver>;

public:
  template<typename Source2, typename Func2, typename Receiver2>
  explicit retry_when_operation(Source2&& source, Func2&& func, Receiver2&& receiver)
      noexcept(std::is_nothrow_constructible_v<Source, Source2> &&
               std::is_nothrow_constructible_v<Func, Func2> &&
               std::is_nothrow_constructible_v<Receiver, Receiver2> &&
               is_nothrow_connectable_v<Source&, source_receiver>)
  : source_((Source2&&)source)
  , func_((Func2&&)func)
  , receiver_((Receiver&&)receiver)
  {
    sourceOp_.construct_from([&] {
        return unifex::connect(source_, source_receiver{this});
      });
  }

  ~retry_when_operation() {
    if (isSourceOpConstructed_) {
      sourceOp_.destruct();
    }
  }

  void start() & noexcept {
    unifex::start(sourceOp_.get());
  }

private:
  friend retry_when_source_receiver<Source, Func, Receiver>;

  template<typename Source2, typename Func2, typename Receiver2, typename Trigger>
  friend class retry_when_trigger_receiver;

  using source_op_t = operation_t<Source&, retry_when_source_receiver<Source, Func, Receiver>>;

  template<typename Error>
  using trigger_sender_t = std::invoke_result_t<Func&, std::remove_cvref_t<Error>>;

  template<typename Error>
  using trigger_receiver_t = retry_when_trigger_receiver<Source, Func, Receiver, trigger_sender_t<Error>>;

  template<typename Error>
  using trigger_op_t = operation_t<
      trigger_sender_t<Error>,
      trigger_receiver_t<Error>>;

  template<typename... Errors>
  using trigger_op_union = manual_lifetime_union<trigger_op_t<Errors>...>;

  UNIFEX_NO_UNIQUE_ADDRESS Source source_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  bool isSourceOpConstructed_ = true;
  union {
    manual_lifetime<source_op_t> sourceOp_;
    typename Source::template error_types<trigger_op_union> triggerOps_;
  };
};

} // namespace detail

template<typename Source, typename Func>
class retry_sender {
  template<typename Error>
  using trigger_sender = std::invoke_result_t<Func&, std::remove_cvref_t<Error>>;

  template<typename... Errors>
  using make_error_type_list = typename concat_type_lists_unique<
      typename trigger_sender<Errors>::template error_types<type_list>...,
      type_list<std::exception_ptr>>::type;

public:
  template<template<typename...> class Variant,
           template<typename...> class Tuple>
  using value_types = typename Source::template value_types<Variant, Tuple>;

  template<template<typename...> class Variant>
  using error_types = typename Source::template error_types<make_error_type_list>::template apply<Variant>;

  template<typename Source2, typename Func2>
  explicit retry_sender(Source2&& source, Func2&& func)
    noexcept(std::is_nothrow_constructible_v<Source, Source2> &&
             std::is_nothrow_constructible_v<Func, Func2>)
  : source_((Source2&&)source)
  , func_((Func2&&)func)
  {}

  // TODO: The connect() methods are currently under-constrained.
  // Ideally they should also check that func() invoked with each of the errors can be connected
  // with the corresponding retry_when_trigger_receiver.

  template<
    typename Receiver,
    typename Op = detail::retry_when_operation<Source, Func, std::remove_cvref_t<Receiver>>,
    std::enable_if_t<
        std::is_move_constructible_v<Source> &&
        std::is_move_constructible_v<Func> &&
        std::is_constructible_v<std::remove_cvref_t<Receiver>, Receiver> &&
        is_connectable_v<Source&, detail::retry_when_source_receiver<Source, Func, std::remove_cvref_t<Receiver>>>, int> = 0>
  Op connect(Receiver&& r) &&
      noexcept(std::is_nothrow_constructible_v<Op, Source, Func, Receiver>) {
    return Op{(Source&&)source_, (Func&&)func_, (Receiver&&)r};
  }

  template<
    typename Receiver,
    typename Op = detail::retry_when_operation<Source, Func, std::remove_cvref_t<Receiver>>,
    std::enable_if_t<
        std::is_constructible_v<Source, Source&> &&
        std::is_constructible_v<Func, Func&> &&
        std::is_constructible_v<std::remove_cvref_t<Receiver>, Receiver> &&
        is_connectable_v<Source&, detail::retry_when_source_receiver<Source, Func, std::remove_cvref_t<Receiver>>>, int> = 0>
  Op connect(Receiver&& r) &
      noexcept(std::is_nothrow_constructible_v<Op, Source&, Func&, Receiver>) {
      return Op{source_, func_, (Receiver&&)r};
  }

  template<
    typename Receiver,
    typename Op = detail::retry_when_operation<Source, Func, std::remove_cvref_t<Receiver>>,
    std::enable_if_t<
        std::is_constructible_v<Source, const Source&> &&
        std::is_constructible_v<Func, const Func&> &&
        std::is_constructible_v<std::remove_cvref_t<Receiver>, Receiver> &&
        is_connectable_v<Source&, detail::retry_when_source_receiver<Source, Func, std::remove_cvref_t<Receiver>>>, int> = 0>
  Op connect(Receiver&& r) &
      noexcept(std::is_nothrow_constructible_v<Op, const Source&, const Func&, Receiver>) {
    return Op{source_, func_, (Receiver&&)r};
  }

private:
  Source source_;
  Func func_;
};

inline constexpr struct retry_when_cpo {
  template<typename Source, typename Func>
  auto operator()(Source&& source, Func&& func) const
      noexcept(is_nothrow_tag_invocable_v<retry_when_cpo, Source, Func>)
      -> tag_invoke_result_t<retry_when_cpo, Source, Func> {
    return tag_invoke(*this, (Source&&)source, (Func&&)func);
  }

  template<
    typename Source,
    typename Func,
    std::enable_if_t<
        !is_tag_invocable_v<retry_when_cpo, Source, Func> &&
        std::is_constructible_v<std::remove_cvref_t<Source>, Source> &&
        std::is_constructible_v<std::remove_cvref_t<Func>, Func>, int> = 0>
  auto operator()(Source&& source, Func&& func) const
      noexcept(std::is_nothrow_constructible_v<
                   retry_sender<std::remove_cvref_t<Source>, std::remove_cvref_t<Func>>,
                   Source, Func>)
      -> retry_sender<std::remove_cvref_t<Source>, std::remove_cvref_t<Func>> {
    return retry_sender<std::remove_cvref_t<Source>, std::remove_cvref_t<Func>>{
        (Source&&)source, (Func&&)func};
  }
} retry_when{};

} // namespace unifex
