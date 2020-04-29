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

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _retry_when {

template <typename Source, typename Func, typename Receiver>
struct _op {
  class type;
};
template <typename Source, typename Func, typename Receiver>
using operation = typename _op<Source, Func, remove_cvref_t<Receiver>>::type;

template <typename Source, typename Func, typename Receiver>
struct _source_receiver {
  class type;
};
template <typename Source, typename Func, typename Receiver>
using source_receiver = typename _source_receiver<Source, Func, Receiver>::type;

template <typename Source, typename Func, typename Receiver, typename Trigger>
struct _trigger_receiver {
  class type;
};
template <typename Source, typename Func, typename Receiver, typename Trigger>
using trigger_receiver =
    typename _trigger_receiver<Source, Func, Receiver, Trigger>::type;

template <typename Source, typename Func, typename Receiver, typename Trigger>
class _trigger_receiver<Source, Func, Receiver, Trigger>::type {
  using trigger_receiver = type;

public:
  explicit type(operation<Source, Func, Receiver>* op) noexcept
    : op_(op) {}

  type(trigger_receiver&& other) noexcept
    : op_(std::exchange(other.op_, nullptr))
  {}

  void set_value() && noexcept {
    assert(op_ != nullptr);

    // This signals to retry the operation.
    auto* op = op_;
    destroy_trigger_op();

    using source_receiver_t = source_receiver<Source, Func, Receiver>;

    if constexpr (is_nothrow_connectable_v<Source&, source_receiver_t>) {
      auto& sourceOp = op->sourceOp_.construct_from([&]() noexcept {
          return unifex::connect(op->source_, source_receiver_t{op});
        });
      op->isSourceOpConstructed_ = true;
      unifex::start(sourceOp);
    } else {
      try {
        auto& sourceOp = op->sourceOp_.construct_from([&] {
            return unifex::connect(op->source_, source_receiver_t{op});
          });
        op->isSourceOpConstructed_ = true;
        unifex::start(sourceOp);
      } catch (...) {
        unifex::set_error((Receiver&&)op->receiver_, std::current_exception());
      }
    }
  }

  template(typename R = Receiver)
    (requires receiver<R>)
  void set_done() && noexcept {
    assert(op_ != nullptr);

    auto* op = op_;
    destroy_trigger_op();
    unifex::set_done((Receiver&&)op->receiver_);
  }

  template(typename Error)
    (requires receiver<Receiver, Error>)
  void set_error(Error error) && noexcept {
    assert(op_ != nullptr);

    auto* op = op_;

    // Note, parameter taken by value so that its lifetime will continue
    // to be valid after we destroy the operation-state that sent it.
    destroy_trigger_op();

    unifex::set_error((Receiver&&)op->receiver_, (Error&&)error);
  }

private:

  template(typename CPO)
    (requires is_receiver_query_cpo_v<CPO> AND
        is_callable_v<CPO, const Receiver&>)
  friend auto tag_invoke(CPO cpo, const trigger_receiver& r)
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(r.get_receiver());
  }

  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const trigger_receiver& r,
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
    using trigger_op = connect_result_t<Trigger, trigger_receiver>;
    op_->triggerOps_.template get<trigger_op>().destruct();
  }

  operation<Source, Func, Receiver>* op_;
};

template <typename Source, typename Func, typename Receiver>
class _source_receiver<Source, Func, Receiver>::type {
  using source_receiver = type;
public:
  explicit type(operation<Source, Func, Receiver>* op) noexcept
  : op_(op) {}

  type(type&& other) noexcept
  : op_(std::exchange(other.op_, {}))
  {}

  template(typename... Values)
    (requires receiver_of<Receiver, Values...>)
  void set_value(Values&&... values)
      noexcept(is_nothrow_receiver_of_v<Receiver, Values...>) {
    assert(op_ != nullptr);
    unifex::set_value(std::move(op_->receiver_), (Values&&)values...);
  }

  void set_done() noexcept {
    assert(op_ != nullptr);
    unifex::set_done(std::move(op_->receiver_));
  }

  template(typename Error)
    (requires std::is_invocable_v<Func&, Error>)
  void set_error(Error error) noexcept {
    assert(op_ != nullptr);
    auto* op = op_;

    op->isSourceOpConstructed_ = false;
    op->sourceOp_.destruct();

    using trigger_sender_t = std::invoke_result_t<Func&, Error>;
    using trigger_receiver_t = trigger_receiver<Source, Func, Receiver, trigger_sender_t>;
    using trigger_op_t = unifex::connect_result_t<trigger_sender_t, trigger_receiver_t>;
    auto& triggerOpStorage = op->triggerOps_.template get<trigger_op_t>();
    if constexpr (std::is_nothrow_invocable_v<Func&, Error> &&
                  is_nothrow_connectable_v<trigger_sender_t, trigger_receiver_t>) {
      auto& triggerOp = triggerOpStorage.construct_from([&]() noexcept {
          return unifex::connect(std::invoke(op->func_, (Error&&)error), trigger_receiver_t{op});
        });
      unifex::start(triggerOp);
    } else {
      try {
        auto& triggerOp = triggerOpStorage.construct_from([&]() {
            return unifex::connect(std::invoke(op->func_, (Error&&)error), trigger_receiver_t{op});
          });
        unifex::start(triggerOp);
      } catch (...) {
        unifex::set_error((Receiver&&)op->receiver_, std::current_exception());
      }
    }
  }

private:
  template(typename CPO)
    (requires is_receiver_query_cpo_v<CPO> AND
        is_callable_v<CPO, const Receiver&>)
  friend auto tag_invoke(CPO cpo, const source_receiver& r)
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(r.get_receiver());
  }

  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const source_receiver& r,
      VisitFunc&& func) noexcept(std::is_nothrow_invocable_v<
                                VisitFunc&,
                                const Receiver&>) {
    std::invoke(func, r.get_receiver());
  }

  const Receiver& get_receiver() const noexcept {
    assert(op_ != nullptr);
    return op_->receiver_;
  }

  operation<Source, Func, Receiver>* op_;
};

template <typename Source, typename Func, typename Receiver>
class _op<Source, Func, Receiver>::type {
  using operation = type;
  using source_receiver_t = source_receiver<Source, Func, Receiver>;
public:
  template <typename Source2, typename Func2, typename Receiver2>
  explicit type(Source2&& source, Func2&& func, Receiver2&& receiver)
      noexcept(std::is_nothrow_constructible_v<Source, Source2> &&
               std::is_nothrow_constructible_v<Func, Func2> &&
               std::is_nothrow_constructible_v<Receiver, Receiver2> &&
               is_nothrow_connectable_v<Source&, source_receiver_t>)
  : source_((Source2&&)source)
  , func_((Func2&&)func)
  , receiver_((Receiver&&)receiver) {
    sourceOp_.construct_from([&] {
        return unifex::connect(source_, source_receiver_t{this});
      });
  }

  ~type() {
    if (isSourceOpConstructed_) {
      sourceOp_.destruct();
    }
  }

  void start() & noexcept {
    unifex::start(sourceOp_.get());
  }

private:
  friend source_receiver_t;

  template <typename Source2, typename Func2, typename Receiver2, typename Trigger>
  friend class _trigger_receiver;

  using source_op_t = connect_result_t<Source&, source_receiver_t>;

  template <typename Error>
  using trigger_sender_t = std::invoke_result_t<Func&, remove_cvref_t<Error>>;

  template <typename Error>
  using trigger_receiver_t = trigger_receiver<Source, Func, Receiver, trigger_sender_t<Error>>;

  template <typename Error>
  using trigger_op_t = connect_result_t<
      trigger_sender_t<Error>,
      trigger_receiver_t<Error>>;

  template <typename... Errors>
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

template <typename Source, typename Func>
struct _sender {
  class type;
};
template <typename Source, typename Func>
using sender = typename _sender<remove_cvref_t<Source>, std::decay_t<Func>>::type;

template <typename Source, typename Func>
class _sender<Source, Func>::type {
  using sender = type;

  template <typename Error>
  using trigger_sender = std::invoke_result_t<Func&, remove_cvref_t<Error>>;

  template <typename... Errors>
  using make_error_type_list = typename concat_type_lists_unique<
      typename trigger_sender<Errors>::template error_types<type_list>...,
      type_list<std::exception_ptr>>::type;

public:
  template <template <typename...> class Variant,
           template <typename...> class Tuple>
  using value_types = typename Source::template value_types<Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename Source::template error_types<make_error_type_list>::template apply<Variant>;

  template <typename Source2, typename Func2>
  explicit type(Source2&& source, Func2&& func)
    noexcept(std::is_nothrow_constructible_v<Source, Source2> &&
             std::is_nothrow_constructible_v<Func, Func2>)
    : source_((Source2&&)source)
    , func_((Func2&&)func)
  {}

  // TODO: The connect() methods are currently under-constrained.
  // Ideally they should also check that func() invoked with each of the errors can be connected
  // with the corresponding trigger_receiver.

  template(typename Self, typename Receiver)
      (requires same_as<remove_cvref_t<Self>, type> AND
          constructible_from<Source, member_t<Self, Source>> AND
          constructible_from<Func, member_t<Self, Func>> AND
          constructible_from<remove_cvref_t<Receiver>, Receiver> AND
          sender_to<Source&, source_receiver<Source, Func, remove_cvref_t<Receiver>>>)
  friend auto tag_invoke(tag_t<connect>, Self&& self, Receiver&& r)
      noexcept(
        std::is_nothrow_constructible_v<Source, member_t<Self, Source>> &&
        std::is_nothrow_constructible_v<Func, member_t<Self, Func>> &&
        std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver> &&
        is_nothrow_connectable_v<Source&, source_receiver<Source, Func, remove_cvref_t<Receiver>>>)
      -> operation<Source, Func, Receiver> {
    return operation<Source, Func, Receiver>{
        static_cast<Self&&>(self).source_, static_cast<Self&&>(self).func_, (Receiver&&)r};
  }

private:
  Source source_;
  Func func_;
};
} // namespace _retry_when

namespace _retry_when_cpo {
  inline const struct _fn {
  private:
    template <bool>
    struct _impl {
      template <typename Source, typename Func>
      auto operator()(Source&& source, Func&& func) const
          noexcept(is_nothrow_tag_invocable_v<_fn, Source, Func>) {
        return unifex::tag_invoke(_fn{}, (Source&&)source, (Func&&)func);
      }
    };
  public:
    template <typename Source, typename Func>
    auto operator()(Source&& source, Func&& func) const
        noexcept(is_nothrow_callable_v<
            _impl<is_tag_invocable_v<_fn, Source, Func>>, Source, Func>)
        -> callable_result_t<
            _impl<is_tag_invocable_v<_fn, Source, Func>>, Source, Func> {
        return _impl<is_tag_invocable_v<_fn, Source, Func>>{}(
          (Source&&)source, (Func&&)func);
      }
  } retry_when{};

  template <>
  struct _fn::_impl<false> {
    template(typename Source, typename Func)
      (requires (!tag_invocable<_fn, Source, Func>) AND
          constructible_from<remove_cvref_t<Source>, Source> AND
          constructible_from<remove_cvref_t<Func>, Func>)
    auto operator()(Source&& source, Func&& func) const
        noexcept(std::is_nothrow_constructible_v<
          _retry_when::sender<Source, Func>, Source, Func>)
        -> _retry_when::sender<Source, Func> {
      return _retry_when::sender<Source, Func>{(Source&&)source, (Func&&)func};
    }
  };
} // namespace _retry_when_cpo
using _retry_when_cpo::retry_when;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
