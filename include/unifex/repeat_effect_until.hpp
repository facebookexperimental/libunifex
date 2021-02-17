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
#include <unifex/functional.hpp>

#include <utility>
#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _repeat_effect_until {
template <typename Source, typename Predicate, typename Receiver>
struct _op {
  class type;
};
template <typename Source, typename Predicate, typename Receiver>
using operation_type = typename _op<Source, Predicate, Receiver>::type;

template <typename Source, typename Predicate, typename Receiver>
struct _rcvr {
  class type;
};
template <typename Source, typename Predicate, typename Receiver>
using receiver_t = typename _rcvr<Source, Predicate, Receiver>::type;

template <typename Source, typename Predicate>
struct _sndr {
  class type;
};

template <typename Source, typename Predicate, typename Receiver>
class _rcvr<Source, Predicate, Receiver>::type {
  using operation = operation_type<Source, Predicate, Receiver>;
public:
  explicit type(operation* op) noexcept
  : op_(op) {}

  type(type&& other) noexcept
  : op_(std::exchange(other.op_, {}))
  {}

  void set_value() noexcept {
    UNIFEX_ASSERT(op_ != nullptr);

    // This signals to repeat_effect_until the operation.
    auto* op = op_;

    UNIFEX_ASSERT(op->isSourceOpConstructed_);
    op->isSourceOpConstructed_ = false;
    op->sourceOp_.destruct();

    if constexpr (is_nothrow_invocable_v<Predicate&> && is_nothrow_connectable_v<Source&, type> && is_nothrow_tag_invocable_v<tag_t<unifex::set_value>, Receiver>) {
      // call predicate and complete with void if it returns true
      if(op->predicate_()) {
        unifex::set_value(std::move(op->receiver_));
        return;
      }
      auto& sourceOp = op->sourceOp_.construct_with([&]() noexcept {
          return unifex::connect(op->source_, type{op});
        });
      op->isSourceOpConstructed_ = true;
      unifex::start(sourceOp);
    } else {
      UNIFEX_TRY {
        // call predicate and complete with void if it returns true
        if(op->predicate_()) {
          unifex::set_value(std::move(op->receiver_));
          return;
        }
        auto& sourceOp = op->sourceOp_.construct_with([&] {
            return unifex::connect(op->source_, type{op});
          });
        op->isSourceOpConstructed_ = true;
        unifex::start(sourceOp);
      } UNIFEX_CATCH (...) {
        unifex::set_error(std::move(op->receiver_), std::current_exception());
      }
    }
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
      noexcept(is_nothrow_invocable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(r.get_rcvr());
  }

  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      VisitFunc&& func) noexcept(is_nothrow_invocable_v<
                                VisitFunc&,
                                const Receiver&>) {
    unifex::invoke(func, r.get_rcvr());
  }

  const Receiver& get_rcvr() const noexcept {
    UNIFEX_ASSERT(op_ != nullptr);   
    return op_->receiver_;
  }

  operation* op_;
};

template <typename Source, typename Predicate, typename Receiver>
class _op<Source, Predicate, Receiver>::type {
  using _receiver_t = receiver_t<Source, Predicate, Receiver>;

public:
  template <typename Source2, typename Predicate2, typename Receiver2>
  explicit type(Source2&& source, Predicate2&& predicate, Receiver2&& dest)
      noexcept(is_nothrow_constructible_v<Receiver, Receiver2> &&
               is_nothrow_constructible_v<Predicate, Predicate2> &&
               is_nothrow_constructible_v<Source, Source2> &&
               is_nothrow_connectable_v<Source&, _receiver_t>)
  : source_((Source2&&)source)
  , predicate_((Predicate2&&)predicate)
  , receiver_((Receiver2&&)dest)
  {
    sourceOp_.construct_with([&] {
        return unifex::connect(source_, _receiver_t{this});
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
  friend _receiver_t;

  using source_op_t = connect_result_t<Source&, _receiver_t>;

  UNIFEX_NO_UNIQUE_ADDRESS Source source_;
  UNIFEX_NO_UNIQUE_ADDRESS Predicate predicate_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  bool isSourceOpConstructed_ = true;
  manual_lifetime<source_op_t> sourceOp_;
};

template <typename Source, typename Predicate>
class _sndr<Source, Predicate>::type {

public:
  template <template <typename...> class Variant,
          template <typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types =
      typename concat_type_lists_unique_t<
          sender_error_types_t<Source, type_list>,
          type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = true;

  template <typename Source2, typename Predicate2>
  explicit type(Source2&& source, Predicate2&& predicate)
    noexcept(
      is_nothrow_constructible_v<Source, Source2> &&
      is_nothrow_constructible_v<Predicate, Predicate2>)
  : source_((Source2&&)source)
  , predicate_((Predicate2&&)predicate)
  {}

  template(typename Sender, typename Receiver)
    (requires same_as<remove_cvref_t<Sender>, type> AND
        constructible_from<remove_cvref_t<Receiver>, Receiver> AND
        sender_to<
            Source&,
            receiver_t<Source, Predicate, remove_cvref_t<Receiver>>>)
  friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r)
       noexcept(
        is_nothrow_constructible_v<Source, decltype((static_cast<Sender&&>(s).source_))> &&
        is_nothrow_constructible_v<Predicate, decltype((static_cast<Sender&&>(s).predicate_))> &&
        is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver> &&
        is_nothrow_connectable_v<Source&, receiver_t<Source, Predicate, remove_cvref_t<Receiver>>>)
        -> operation_type<Source, Predicate, remove_cvref_t<Receiver>> {
    return operation_type<Source, Predicate, remove_cvref_t<Receiver>>{
      static_cast<Sender&&>(s).source_,
      static_cast<Sender&&>(s).predicate_,
      (Receiver&&)r
    };
  }

private:
  UNIFEX_NO_UNIQUE_ADDRESS Source source_;
  UNIFEX_NO_UNIQUE_ADDRESS Predicate predicate_;
};

} // namespace _repeat_effect_until

template <class Source, class Predicate>
using repeat_effect_until_sender = typename _repeat_effect_until::_sndr<Source, Predicate>::type;

inline const struct repeat_effect_until_cpo {
  template <typename Source, typename Predicate>
  auto operator()(Source&& source, Predicate&& predicate) const
      noexcept(is_nothrow_tag_invocable_v<repeat_effect_until_cpo, Source, Predicate>)
      -> tag_invoke_result_t<repeat_effect_until_cpo, Source, Predicate> {
    return tag_invoke(*this, (Source&&)source, (Predicate&&)predicate);
  }

  template(typename Source, typename Predicate)
    (requires (!tag_invocable<repeat_effect_until_cpo, Source, Predicate>) AND
        constructible_from<remove_cvref_t<Source>, Source> AND
        constructible_from<std::decay_t<Predicate>, Predicate>)
  auto operator()(Source&& source, Predicate&& predicate) const
      noexcept(is_nothrow_constructible_v<
                   repeat_effect_until_sender<remove_cvref_t<Source>, std::decay_t<Predicate>>,
                   Source,
                   Predicate>)
      -> repeat_effect_until_sender<remove_cvref_t<Source>, std::decay_t<Predicate>> {
    return repeat_effect_until_sender<remove_cvref_t<Source>, std::decay_t<Predicate>>{
        (Source&&)source, (Predicate&&)predicate};
  }
  template <typename Predicate>
  constexpr auto operator()(Predicate&& predicate) const
      noexcept(is_nothrow_callable_v<
        tag_t<bind_back>, repeat_effect_until_cpo, Predicate>)
      -> bind_back_result_t<repeat_effect_until_cpo, Predicate> {
    return bind_back(*this, (Predicate&&)predicate);
  }
} repeat_effect_until{};

inline const struct repeat_effect_cpo {
  struct forever {
    bool operator()() const { return false; }
  };
  template <typename Source>
  auto operator()(Source&& source) const
      noexcept(is_nothrow_tag_invocable_v<repeat_effect_cpo, Source>)
      -> tag_invoke_result_t<repeat_effect_cpo, Source> {
    return tag_invoke(*this, (Source&&)source);
  }

  template(typename Source)
    (requires (!tag_invocable<repeat_effect_cpo, Source>) AND
        constructible_from<remove_cvref_t<Source>, Source>)
  auto operator()(Source&& source) const
      noexcept(is_nothrow_constructible_v<
                   repeat_effect_until_sender<remove_cvref_t<Source>, forever>,
                   Source>)
      -> repeat_effect_until_sender<remove_cvref_t<Source>, forever> {
    return repeat_effect_until_sender<remove_cvref_t<Source>, forever>{
        (Source&&)source, forever{}};
  }
  constexpr auto operator()() const
      noexcept(is_nothrow_callable_v<
        tag_t<bind_back>, repeat_effect_cpo>)
      -> bind_back_result_t<repeat_effect_cpo> {
    return bind_back(*this);
  }
} repeat_effect{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
