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
#include <unifex/async_trace.hpp>

#include <utility>
#include <cassert>
#include <exception>

namespace unifex {
namespace _repeat_effect_until {
template<typename Source, typename Predicate, typename Receiver>
struct _op {
  class type;
};
template<typename Source, typename Predicate, typename Receiver>
using operation = typename _op<Source, Predicate, std::remove_cvref_t<Receiver>>::type;

template<typename Source, typename Predicate, typename Receiver>
struct _rcvr {
  class type;
};
template<typename Source, typename Predicate, typename Receiver>
using receiver = typename _rcvr<Source, Predicate, std::remove_cvref_t<Receiver>>::type;

template<typename Source, typename Predicate>
struct _sndr {
  class type;
};

template<typename Source, typename Predicate, typename Receiver>
class _rcvr<Source, Predicate, Receiver>::type {
  using operation = operation<Source, Predicate, Receiver>;
public:
  explicit type(operation* op) noexcept
  : op_(op) {}

  type(type&& other) noexcept
  : op_(std::exchange(other.op_, {}))
  {}
 
  void set_value() noexcept {
    assert(op_ != nullptr);

    // This signals to repeat_effect_until the operation.
    auto* op = op_;

    if (op->isSourceOpConstructed_) {
      op->sourceOp_.destruct();
      op->isSourceOpConstructed_ = false;
    }

    // call predicate and complete with void if it returns true
    if(op->predicate_()) {
      if constexpr (std::is_nothrow_invocable_v<tag_t<unifex::set_value>&, Receiver>) {
        unifex::set_value(std::move(op->receiver_));
      } else {
        try {
          unifex::set_value(std::move(op->receiver_));
        } catch (...) {
          unifex::set_error((Receiver&&)op->receiver_, std::current_exception());
        }
      }
      return;
    }

    if constexpr (is_nothrow_connectable_v<Source&, type>) {
      auto& sourceOp = op->sourceOp_.construct_from([&]() noexcept {
          return unifex::connect(op->source_, type{op});
        });
      op->isSourceOpConstructed_ = true;
      unifex::start(sourceOp);
    } else {
      try {
        auto& sourceOp = op->sourceOp_.construct_from([&] {
            return unifex::connect(op->source_, type{op});
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
    std::enable_if_t<std::is_invocable_v<decltype(unifex::set_done), R>, int> = 0>
  void set_done() noexcept {
    assert(op_ != nullptr);
    unifex::set_done(std::move(op_->receiver_));
  }

  template<
    typename Error,
    std::enable_if_t<std::is_invocable_v<decltype(unifex::set_error), Receiver, Error>, int> = 0>
  void set_error(Error&& error) noexcept {
    assert(op_ != nullptr);
    unifex::set_error(std::move(op_->receiver_), (Error&&)error);
  }

private:
  template<
    typename CPO, 
    typename... Args, 
    std::enable_if_t<
      is_callable_v<CPO, const Receiver&, Args...>, int> = 0>
  friend auto tag_invoke(CPO cpo, const type& r, Args&&... args)
      noexcept(std::is_nothrow_invocable_v<CPO, const Receiver&, Args...>)
      -> std::invoke_result_t<CPO, const Receiver&, Args...> {
    return std::move(cpo)(r.get_rcvr(), (Args&&)args...);
  }
  
  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      VisitFunc&& func) noexcept(std::is_nothrow_invocable_v<
                                VisitFunc&,
                                const Receiver&>) {
    std::invoke(func, r.get_rcvr());
  }

  const Receiver& get_rcvr() const noexcept {
    assert(op_ != nullptr);   
    return op_->receiver_;
  }

  operation* op_;
};

template<typename Source, typename Predicate, typename Receiver>
class _op<Source, Predicate, Receiver>::type {
  using receiver = receiver<Source, Predicate, Receiver>;

public:
  template<typename Source2, typename Predicate2, typename Receiver2>
  explicit type(Source2&& source, Predicate2&& predicate, Receiver2&& dest)
      noexcept(std::is_nothrow_constructible_v<Source, Source2> &&
               std::is_nothrow_constructible_v<Predicate, Predicate2> &&
               std::is_nothrow_constructible_v<Receiver, Receiver2> &&
               is_nothrow_connectable_v<Source&, receiver>)
  : source_((Source2&&)source)
  , predicate_((Predicate&&)predicate)
  , receiver_((Receiver&&)dest)
  {
    sourceOp_.construct_from([&] {
        return unifex::connect(source_, receiver{this});
      });
  }

  ~type() {
    if (isSourceOpConstructed_) {
      sourceOp_.destruct();
      isSourceOpConstructed_ = false;
    }
  }

  void start() & noexcept {
    unifex::start(sourceOp_.get());
  }

private:
  friend receiver;

  using source_op_t = operation_t<Source&, receiver>;

  UNIFEX_NO_UNIQUE_ADDRESS Source source_;
  UNIFEX_NO_UNIQUE_ADDRESS Predicate predicate_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  bool isSourceOpConstructed_ = true;
  manual_lifetime<source_op_t> sourceOp_;
};

template<typename Source, typename Predicate>
class _sndr<Source, Predicate>::type {

public:
  template<template<typename...> class Variant,
          template<typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template<template<typename...> class Variant>
  using error_types = typename Source::template error_types<Variant>;

  template<typename Source2, typename Predicate2>
  explicit type(Source2&& source, Predicate2&& predicate)
    noexcept(std::is_nothrow_constructible_v<Source, Source2, Predicate2>)
  : source_((Source2&&)source)
  , predicate_((Predicate&&)predicate)
  {}

  template<
    typename Receiver,
    typename Op = operation<Source, Predicate, std::remove_cvref_t<Receiver>>>
  Op connect(Receiver&& r) &&
      noexcept(std::is_nothrow_constructible_v<Op, Source, Predicate, Receiver>) {
    return Op{(Source&&)source_, (Predicate&&)predicate_, (Receiver&&)r};
  }

  template<
    typename Receiver,
    typename Op = operation<Source, Predicate, std::remove_cvref_t<Receiver>>>
  Op connect(Receiver&& r) const &
      noexcept(std::is_nothrow_constructible_v<Op, const Source&, const Predicate&, Receiver>) {
      return Op{source_, predicate_, (Receiver&&)r};
  }

private:
  Source source_;
  Predicate predicate_;
};

} // namespace _reapeat

template<class Source, class Predicate>
using repeat_effect_until_sender = typename _repeat_effect_until::_sndr<std::remove_cvref_t<Source>, std::remove_cvref_t<Predicate>>::type;

inline constexpr struct repeat_effect_until_cpo {
  template<typename Source, typename Predicate>
  auto operator()(Source&& source, Predicate&& predicate) const
      noexcept(is_nothrow_tag_invocable_v<repeat_effect_until_cpo, Source, Predicate>)
      -> tag_invoke_result_t<repeat_effect_until_cpo, Source, Predicate> {
    return tag_invoke(*this, (Source&&)source, (Predicate&&)predicate);
  }

  template<
    typename Source,
    typename Predicate,
    std::enable_if_t<
        !is_tag_invocable_v<repeat_effect_until_cpo, Source> &&
        std::is_constructible_v<std::remove_cvref_t<Source>, Source> &&
        std::is_constructible_v<std::remove_cvref_t<Predicate>, Predicate>, int> = 0>
  auto operator()(Source&& source, Predicate&& predicate) const
      noexcept(std::is_nothrow_constructible_v<
                   repeat_effect_until_sender<Source, Predicate>,
                   Source, 
                   Predicate>)
      -> repeat_effect_until_sender<Source, Predicate> {
    return repeat_effect_until_sender<Source, Predicate>{
        (Source&&)source, (Predicate&&)predicate};
  }
} repeat_effect_until{};

inline constexpr struct repeat_effect_cpo {
  struct forever {
    bool operator()() const { return false; }
  };
  template<typename Source, typename Predicate>
  auto operator()(Source&& source) const
      noexcept(is_nothrow_tag_invocable_v<repeat_effect_cpo, Source>)
      -> tag_invoke_result_t<repeat_effect_cpo, Source> {
    return tag_invoke(*this, (Source&&)source);
  }

  template<
    typename Source,
    std::enable_if_t<
        !is_tag_invocable_v<repeat_effect_cpo, Source> &&
        std::is_constructible_v<std::remove_cvref_t<Source>, Source>, int> = 0>
  auto operator()(Source&& source) const
      noexcept(std::is_nothrow_constructible_v<
                   repeat_effect_until_sender<Source, forever>,
                   Source>)
      -> repeat_effect_until_sender<Source, forever> {
    return repeat_effect_until_sender<Source, forever>{
        (Source&&)source, forever{}};
  }
} repeat_effect{};

} // namespace unifex
