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
#include <unifex/execution_policy.hpp>
#include <unifex/get_execution_policy.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_list.hpp>
#include <unifex/inplace_stop_token.hpp>

#include <variant>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _bulk_via {
template <typename... Errors>
using unique_decayed_error_types =
    concat_type_lists_unique_t<type_list<std::decay_t<Errors>>...>;

template <template <typename...> class Variant, typename... Senders>
using error_types = typename concat_type_lists_unique_t<
    sender_error_types_t<Senders, unique_decayed_error_types>...,
    type_list<std::exception_ptr>>::template apply<Variant>;

template <typename... Values>
using decayed_value_tuple = type_list<std::tuple<std::decay_t<Values>...>>;

template <typename Sender>
using value_variant_for_sender = typename sender_value_types_t<
    Sender, concat_type_lists_unique_t,
    decayed_value_tuple>::template apply<std::variant>;

template <typename Scheduler, typename Source, typename Receiver> struct _op {
  class type;
};

template <typename Scheduler, typename Source, typename Receiver>
using operation =
    typename _op<Scheduler, Source, remove_cvref_t<Receiver>>::type;

struct cancel_callback {
  inplace_stop_source &stopSource_;

  void operator()() noexcept { stopSource_.request_stop(); }
};

template <typename Scheduler, typename Source, typename Receiver,
          typename... Values>
struct _next_receiver {
  struct type;
};

template <typename Scheduler, typename Source, typename Receiver,
          typename... Values>
using next_receiver = typename _next_receiver<Scheduler, Source, Receiver,
                                              std::decay_t<Values>...>::type;

template <typename Scheduler, typename Source, typename Receiver,
          typename... Values>
struct _next_receiver<Scheduler, Source, Receiver, Values...>::type {
  using next_receiver = type;
  using operation_type = operation<Scheduler, Source, Receiver>;
  operation_type *op_;
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;

  void set_value() noexcept {
    std::apply([&](auto &&... values) {
      unifex::set_next(op_->receiver_, values...);
    }, std::move(values_));

    op_->element_complete();
  }

  template <typename Error> void set_error(Error &&error) noexcept {
    op_->set_error(static_cast<Error &&>(error));
    op_->element_complete();
  }

  void set_done() noexcept {
    op_->set_done();
    op_->element_complete();
  }

  template(typename CPO)(requires is_receiver_query_cpo_v<CPO>) friend auto tag_invoke(
      CPO cpo, const next_receiver
                   &r) noexcept(is_nothrow_callable_v<CPO, const Receiver &>)
      -> callable_result_t<CPO, const Receiver &> {
    return std::move(cpo)(std::as_const(r.get_receiver()));
  }

  const Receiver &get_receiver() const noexcept { return op_->receiver_; }

  friend inplace_stop_token tag_invoke(tag_t<unifex::get_stop_token>,
                                       const type &r) noexcept {
    return r.get_stop_token();
  }

  inplace_stop_token get_stop_token() const noexcept {
    return op_->stopSource_.get_token();
  }

  template <typename Func>
  friend void tag_invoke(tag_t<visit_continuations>, const next_receiver &r,
                         Func &&func) {
    std::invoke(func, r.context_->receiver_);
  }
};

template <typename Scheduler, typename Source, typename Receiver,
          typename... Values>
struct _value_receiver {
  struct type;
};
template <typename Scheduler, typename Source, typename Receiver,
          typename... Values>
using value_receiver = typename _value_receiver<Scheduler, Source, Receiver,
                                                std::decay_t<Values>...>::type;

template <typename Scheduler, typename Source, typename Receiver,
          typename... Values>
struct _value_receiver<Scheduler, Source, Receiver, Values...>::type {
  using value_receiver = type;
  using operation_type = operation<Scheduler, Source, Receiver>;
  operation_type *op_;
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;

  void set_value() noexcept {
    op_->value_ = std::move(values_);
    op_->element_complete();
  }

  template <typename Error> void set_error(Error &&error) noexcept {
    op_->set_error(static_cast<Error &&>(error));
    op_->element_complete();
  }

  void set_done() noexcept {
    op_->set_done();
    op_->element_complete();
  }

  template(typename CPO)(requires is_receiver_query_cpo_v<CPO>) friend auto tag_invoke(
      CPO cpo, const value_receiver
                   &r) noexcept(is_nothrow_callable_v<CPO, const Receiver &>)
      -> callable_result_t<CPO, const Receiver &> {
    return std::move(cpo)(std::as_const(r.get_receiver()));
  }

  const Receiver &get_receiver() const noexcept { return op_->receiver_; }

  friend inplace_stop_token tag_invoke(tag_t<unifex::get_stop_token>,
                                       const type &r) noexcept {
    return r.get_stop_token();
  }

  inplace_stop_token get_stop_token() const noexcept {
    return op_->stopSource_.get_token();
  }

  template <typename Func>
  friend void tag_invoke(tag_t<visit_continuations>, const value_receiver &r,
                         Func &&func) {
    std::invoke(func, r.op_->receiver_);
  }
};

template <typename Scheduler, typename Source, typename Receiver,
          typename Error>
struct _error_receiver {
  struct type;
};
template <typename Scheduler, typename Source, typename Receiver,
          typename Error>
using error_receiver = typename _error_receiver<Scheduler, Source, Receiver,
                                                std::decay_t<Error>>::type;

template <typename Scheduler, typename Source, typename Receiver,
          typename Error>
struct _error_receiver<Scheduler, Source, Receiver, Error>::type {
  using error_receiver = type;
  using operation_type = operation<Scheduler, Source, Receiver>;
  operation_type *op_;
  UNIFEX_NO_UNIQUE_ADDRESS Error error_;

  void set_value() noexcept {
    op_->error_ = std::move(error_);
    op_->element_complete();
  }

  template <typename OtherError>
  void set_error(OtherError &&otherError) noexcept {
    op_->set_error(static_cast<OtherError &&>(otherError));
    op_->element_complete();
  }

  void set_done() noexcept {
    op_->set_done();
    op_->element_complete();
  }

  template(typename CPO)(requires is_receiver_query_cpo_v<CPO>) friend auto tag_invoke(
      CPO cpo, const error_receiver
                   &r) noexcept(is_nothrow_callable_v<CPO, const Receiver &>)
      -> callable_result_t<CPO, const Receiver &> {
    return std::move(cpo)(std::as_const(r.get_receiver()));
  }

  const Receiver &get_receiver() const noexcept { return op_->receiver_; }

  friend inplace_stop_token tag_invoke(tag_t<unifex::get_stop_token>,
                                       const type &r) noexcept {
    return r.get_stop_token();
  }

  inplace_stop_token get_stop_token() const noexcept {
    return op_->stopSource_.get_token();
  }

  template <typename Func>
  friend void tag_invoke(tag_t<visit_continuations>, const error_receiver &r,
                         Func &&func) {
    std::invoke(func, r.op_->receiver_);
  }
};

template <typename Scheduler, typename Source, typename Receiver>
struct _done_receiver {
  struct type;
};
template <typename Scheduler, typename Source, typename Receiver>
using done_receiver =
    typename _done_receiver<Scheduler, Source, Receiver>::type;

template <typename Scheduler, typename Source, typename Receiver>
struct _done_receiver<Scheduler, Source, Receiver>::type {
  using done_receiver = type;
  using operation_type = operation<Scheduler, Source, Receiver>;
  operation_type *op_;

  void set_value() noexcept { op_->element_complete(); }

  template <typename OtherError>
  void set_error(OtherError &&otherError) noexcept {
    op_->set_error(static_cast<OtherError &&>(otherError));
    op_->element_complete();
  }

  void set_done() noexcept {
    op_->set_done();
    op_->element_complete();
  }

  template(typename CPO)(requires is_receiver_query_cpo_v<CPO>) friend auto tag_invoke(
      CPO cpo, const done_receiver
                   &r) noexcept(is_nothrow_callable_v<CPO, const Receiver &>)
      -> callable_result_t<CPO, const Receiver &> {
    return std::move(cpo)(std::as_const(r.get_receiver()));
  }

  const Receiver &get_receiver() const noexcept { return op_->receiver_; }

  friend inplace_stop_token tag_invoke(tag_t<unifex::get_stop_token>,
                                       const type &r) noexcept {
    return r.get_stop_token();
  }

  inplace_stop_token get_stop_token() const noexcept {
    return op_->stopSource_.get_token();
  }

  template <typename Func>
  friend void tag_invoke(tag_t<visit_continuations>, const done_receiver &r,
                         Func &&func) {
    std::invoke(func, r.op_->receiver_);
  }
};

template <typename Scheduler, typename Source, typename Receiver>
struct _predecessor_receiver {
  class type;
};

template <typename Scheduler, typename Source, typename Receiver>
using predecessor_receiver =
    typename _predecessor_receiver<Scheduler, Source, Receiver>::type;

template <typename Scheduler, typename Source, typename Receiver>
class _predecessor_receiver<Scheduler, Source, Receiver>::type {
  using predecessor_receiver = type;
  using operation_type = operation<Scheduler, Source, Receiver>;

public:
  template <template <typename...> class Variant,
            template <typename...> class Tuple>
  using next_types = sender_next_types_t<Source, Variant, Tuple>;

  template <template <typename...> class Variant,
            template <typename...> class Tuple>
  using value_types = sender_value_types_t<Source, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<Source, Variant>;

  template <typename Scheduler2>
  explicit type(Scheduler2 &&s, operation_type *op)
      : scheduler_(static_cast<Scheduler2 &&>(s)), op_(op) {}

  void set_next() & noexcept(is_nothrow_next_receiver_v<Receiver>) {
    op_->element_start();
    UNIFEX_TRY {
      submit(schedule(scheduler_),
             next_receiver<Scheduler, Source, Receiver>{op_});
    }
    UNIFEX_CATCH(...) {
      op_->set_error(std::current_exception());
      op_->element_complete();
    }
  }

  template(typename... Values)(
      requires (!std::is_void_v<Values...>)) void set_next(Values &&... values) &
      noexcept(is_nothrow_next_receiver_v<Receiver, Values...>) {
    op_->element_start();
    UNIFEX_TRY {
      submit(schedule(scheduler_),
             next_receiver<Scheduler, Source, Receiver, Values...>{
                 op_, static_cast<Values &&>(values)...});
    }
    UNIFEX_CATCH(...) {
      op_->set_error(std::current_exception());
      op_->element_complete();
    }
  }

  template(typename... Values)(
      requires receiver_of<Receiver,
                           Values...>) void set_value(Values &&... values) &&
      noexcept(is_nothrow_receiver_of_v<Receiver, Values...>) {
    UNIFEX_TRY {
      submit(schedule(scheduler_),
             value_receiver<Scheduler, Source, Receiver, Values...>{
                 op_, static_cast<Values &&>(values)...});
    }
    UNIFEX_CATCH(...) {
      op_->set_error(std::current_exception());
      op_->element_complete();
    }
  }

  template(typename Error)(requires receiver<Receiver, Error>) void set_error(
      Error &&error) &&
      noexcept {
    UNIFEX_TRY {
      submit(schedule(scheduler_),
             error_receiver<Scheduler, Source, Receiver, Error>{
                 op_, static_cast<Error &&>(error)});
    }
    UNIFEX_CATCH(...) {
      op_->set_error(std::current_exception());
      op_->element_complete();
    }
  }

  void set_done() && noexcept {
    UNIFEX_TRY {
      submit(schedule(scheduler_),
             done_receiver<Scheduler, Source, Receiver>{op_});
    }
    UNIFEX_CATCH(...) {
      op_->set_error(std::current_exception());
      op_->element_complete();
    }
  }

  template(typename CPO, typename Self)(
      requires is_receiver_query_cpo_v<CPO> AND same_as<
          Self,
          type>) friend auto tag_invoke(CPO cpo,
                                        const Self &
                                            self) noexcept(is_nothrow_callable_v<CPO,
                                                                                 const Receiver
                                                                                     &>)
      -> callable_result_t<CPO, const Receiver &> {
    return cpo(self.op_->receiver_);
  }

  friend inplace_stop_token tag_invoke(tag_t<unifex::get_stop_token>,
                                       const type &r) noexcept {
    return r.get_stop_token();
  }

  friend auto tag_invoke(tag_t<unifex::get_execution_policy>,
                         const type &r) noexcept {
    return r.get_execution_policy();
  }

  template <typename Func>
  friend void tag_invoke(tag_t<visit_continuations>,
                         const predecessor_receiver &r, Func &&func) {
    std::invoke(func, r.op_->receiver_);
  }

private:
  inplace_stop_token get_stop_token() const noexcept {
    return op_->stopSource_.get_token();
  }

  constexpr auto get_execution_policy() const noexcept {
    return unifex::get_execution_policy(op_->receiver_);
  }

  operation_type *op_;
  UNIFEX_NO_UNIQUE_ADDRESS Scheduler scheduler_;
};

template <typename Scheduler, typename Source, typename Receiver>
class _op<Scheduler, Source, Receiver>::type {
  using receiver_type = predecessor_receiver<Scheduler, Source, Receiver>;

public:
  template <typename Scheduler2, typename Receiver2>
  explicit type(Scheduler2 &&scheduler, Source &&source, Receiver2 &&receiver)
      : innerOp_(connect(
            static_cast<Source &&>(source),
            receiver_type{static_cast<Scheduler2 &&>(scheduler), this})),
        receiver_(static_cast<Receiver2 &&>(receiver)) {}

  void start() & noexcept {
    stopCallback_.emplace(get_stop_token(receiver_),
                          cancel_callback{stopSource_});
    unifex::start(innerOp_);
  }

  void element_start() noexcept {
    refCount_.fetch_add(1, std::memory_order_relaxed);
  }

  void element_complete() noexcept {
    if (!refCount_.fetch_sub(1, std::memory_order_release)) {
      deliver_result();
    }
  }

  template(typename Error)(requires receiver<Receiver, Error>) void set_error(
      Error &&error) noexcept {
    if (!doneOrError_.exchange(true, std::memory_order_relaxed)) {
      error_.emplace(std::in_place_type<std::decay_t<Error>>,
                     static_cast<Error &&>(error));
      stopSource_.request_stop();
    }
  }

  void set_done() noexcept {
    if (!doneOrError_.exchange(true, std::memory_order_relaxed)) {
      stopSource_.request_stop();
    }
  }

  void deliver_result() noexcept {
    stopCallback_.reset();

    if (get_stop_token(receiver_).stop_requested()) {
      unifex::set_done(std::move(receiver_));
    } else if (doneOrError_.load(std::memory_order_relaxed)) {
      if (error_.has_value()) {
        unifex::set_error(std::move(receiver_), std::move(error_));
      } else {
        unifex::set_done(std::move(receiver_));
      }
    } else {
      std::apply([&](auto &&... values) {
        unifex::set_value(std::move(receiver_), values...);
      }, std::move(std::get<0>(value_.value())));
    }
  }

private:
  friend class _predecessor_receiver<Scheduler, Source, Receiver>::type;
  template <typename, typename, typename, typename...>
  friend struct _next_receiver;
  template <typename, typename, typename, typename...>
  friend struct _value_receiver;
  template <typename, typename, typename, typename>
  friend struct _error_receiver;
  template <typename, typename, typename> friend struct _done_receiver;

  connect_result_t<Source, receiver_type> innerOp_;
  Receiver receiver_;
  std::optional<value_variant_for_sender<remove_cvref_t<Source>>> value_;
  std::optional<error_types<std::variant, remove_cvref_t<Source>>> error_;
  std::atomic_size_t refCount_{0};
  inplace_stop_source stopSource_;
  std::optional<typename stop_token_type_t<Receiver>::template callback_type<
      cancel_callback>> stopCallback_;
  std::atomic_bool doneOrError_{false};
};

template <typename Scheduler, typename Source> struct _default_sender {
  class type;
};

template <typename Scheduler, typename Source>
using default_sender = typename _default_sender<Scheduler, Source>::type;

template <typename Scheduler, typename Source>
class _default_sender<Scheduler, Source>::type {
public:
  template <template <typename...> class Variant,
            template <typename...> class Tuple>
  using next_types = sender_next_types_t<Source, Variant, Tuple>;

  template <template <typename...> class Variant,
            template <typename...> class Tuple>
  using value_types = sender_value_types_t<Source, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<Source, Variant>;

  static constexpr bool sends_done = true;

  template <typename Scheduler2, typename Source2>
  explicit type(Scheduler2 &&scheduler, Source2 &&source)
      : scheduler_(static_cast<Scheduler2 &&>(scheduler)),
        source_(static_cast<Source2 &&>(source)) {}

  template(typename Self, typename Receiver)(
      requires same_as<remove_cvref_t<Self>, type> AND receiver<Receiver> AND sender_to<
          member_t<Self, Source>,
          predecessor_receiver<
              remove_cvref_t<Scheduler>, remove_cvref_t<Source>,
              remove_cvref_t<
                  Receiver>>>) friend auto tag_invoke(tag_t<connect>,
                                                      Self &&self,
                                                      Receiver &&
                                                          r) noexcept(std::
                                                                          is_nothrow_constructible_v<
                                                                              Source,
                                                                              member_t<
                                                                                  Self,
                                                                                  Source>>
                                                                              &&std::is_nothrow_constructible_v<
                                                                                  remove_cvref_t<
                                                                                      Receiver>,
                                                                                  Receiver>) {
    return operation<Scheduler, Source, Receiver>{
        static_cast<Scheduler &&>(static_cast<Self &&>(self).scheduler_),
        static_cast<Source &&>(static_cast<Self &&>(self).source_),
        static_cast<Receiver &&>(r)};
  }

private:
  UNIFEX_NO_UNIQUE_ADDRESS Scheduler scheduler_;
  UNIFEX_NO_UNIQUE_ADDRESS Source source_;
};

struct _fn {
  template(typename Scheduler, typename Source)(
      requires scheduler<Scheduler> AND typed_bulk_sender<Source> AND
          tag_invocable<_fn, Scheduler, Source>) auto
  operator()(Scheduler &&sch, Source &&s) const
      noexcept(is_nothrow_tag_invocable_v<_fn, Scheduler, Source>)
          -> tag_invoke_result_t<_fn, Scheduler, Source> {
    return tag_invoke(_fn{}, static_cast<Scheduler &&>(sch),
                      static_cast<Source &&>(s));
  }

  template(typename Scheduler, typename Source)(
      requires scheduler<Scheduler> AND typed_bulk_sender<Source> AND(
          !tag_invocable<_fn, Scheduler, Source>)) auto
  operator()(Scheduler &&sch, Source &&s) const
      noexcept(std::is_nothrow_constructible_v<remove_cvref_t<Source>, Source>)
          -> default_sender<remove_cvref_t<Scheduler>, remove_cvref_t<Source>> {
    return default_sender<remove_cvref_t<Scheduler>, remove_cvref_t<Source>>{
        static_cast<Scheduler &&>(sch), static_cast<Source &&>(s)};
  }
};

} // namespace _bulk_via

inline constexpr _bulk_via::_fn bulk_via{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
