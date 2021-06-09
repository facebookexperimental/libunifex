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
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/submit.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <tuple>
#include <utility>
#include <exception>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _via {

constexpr blocking_kind _blocking_kind(blocking_kind predBlocking, blocking_kind succBlocking) noexcept {
  if (predBlocking == blocking_kind::never &&
      succBlocking == blocking_kind::never) {
    return blocking_kind::never;
  } else if (
      predBlocking == blocking_kind::always_inline &&
      succBlocking == blocking_kind::always_inline) {
    return blocking_kind::always_inline;
  } else if (
      (predBlocking == blocking_kind::always_inline ||
        predBlocking == blocking_kind::always) &&
      (succBlocking == blocking_kind::always_inline ||
        succBlocking == blocking_kind::always)) {
    return blocking_kind::always;
  } else {
    return blocking_kind::maybe;
  }
}

template <typename Receiver, typename... Values>
struct _value_receiver {
  struct type;
};
template <typename Receiver, typename... Values>
using value_receiver = typename _value_receiver<
    Receiver,
    std::decay_t<Values>...>::type;

template <typename Receiver, typename... Values>
struct _value_receiver<Receiver, Values...>::type {
  using value_receiver = type;
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  void set_value() noexcept {
    std::apply(
        [&](Values && ... values) noexcept {
          unifex::set_value(
              std::forward<Receiver>(receiver_), (Values &&) values...);
        },
        std::move(values_));
  }

  template <typename Error>
  void set_error(Error&& error) noexcept {
    unifex::set_error(std::forward<Receiver>(receiver_), (Error &&) error);
  }

  void set_done() noexcept {
    unifex::set_done(std::forward<Receiver>(receiver_));
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const value_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const value_receiver& r,
      Func&& func) {
    std::invoke(func, r.receiver_);
  }
#endif
};

template <typename Receiver, typename Error>
struct _error_receiver {
  struct type;
};
template <typename Receiver, typename Error>
using error_receiver = typename _error_receiver<Receiver, std::decay_t<Error>>::type;

template <typename Receiver, typename Error>
struct _error_receiver<Receiver, Error>::type {
  using error_receiver = type;
  UNIFEX_NO_UNIQUE_ADDRESS Error error_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  void set_value() noexcept {
    unifex::set_error(std::forward<Receiver>(receiver_), std::move(error_));
  }

  template <typename OtherError>
  void set_error(OtherError&& otherError) noexcept {
    unifex::set_error(
        std::forward<Receiver>(receiver_), (OtherError &&) otherError);
  }

  void set_done() noexcept {
    unifex::set_done(std::forward<Receiver>(receiver_));
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const error_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const error_receiver& r,
      Func&& func) {
    std::invoke(func, r.receiver_);
  }
#endif
};

template <typename Receiver>
struct _done_receiver {
  struct type;
};
template <typename Receiver>
using done_receiver = typename _done_receiver<Receiver>::type;

template <typename Receiver>
struct _done_receiver<Receiver>::type {
  using done_receiver = type;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  void set_value() noexcept {
    unifex::set_done(std::forward<Receiver>(receiver_));
  }

  template <typename OtherError>
  void set_error(OtherError&& otherError) noexcept {
    unifex::set_error(
        std::forward<Receiver>(receiver_), (OtherError &&) otherError);
  }

  void set_done() noexcept {
    unifex::set_done(std::forward<Receiver>(receiver_));
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const done_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const done_receiver& r,
      Func&& func) {
    std::invoke(func, r.receiver_);
  }
#endif
};

template <typename Successor, typename Receiver>
struct _predecessor_receiver {
  struct type;
};
template <typename Successor, typename Receiver>
using predecessor_receiver =
    typename _predecessor_receiver<Successor, remove_cvref_t<Receiver>>::type;

template <typename Successor, typename Receiver>
struct _predecessor_receiver<Successor, Receiver>::type {
  using predecessor_receiver = type;
  Successor successor_;
  Receiver receiver_;

  template <typename... Values>
  void set_value(Values&&... values) && noexcept {
    UNIFEX_TRY {
      submit(
          (Successor &&) successor_,
          value_receiver<Receiver, Values...>{
              {(Values &&) values...}, (Receiver &&) receiver_});
    } UNIFEX_CATCH (...) {
      unifex::set_error(
          static_cast<Receiver&&>(receiver_), std::current_exception());
    }
  }

  template <typename Error>
  void set_error(Error&& error) && noexcept {
    UNIFEX_TRY {
      submit(
          (Successor &&) successor_,
          error_receiver<Receiver, Error>{
              (Error &&) error, (Receiver &&) receiver_});
    } UNIFEX_CATCH (...) {
      unifex::set_error(
          static_cast<Receiver&&>(receiver_), std::current_exception());
    }
  }

  void set_done() && noexcept {
    UNIFEX_TRY {
      submit(
          (Successor &&) successor_,
          done_receiver<Receiver>{(Receiver &&) receiver_});
    } UNIFEX_CATCH (...) {
      unifex::set_error(
          static_cast<Receiver&&>(receiver_), std::current_exception());
    }
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const predecessor_receiver& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const predecessor_receiver& r,
      Func&& func) {
    std::invoke(func, r.receiver_);
  }
#endif
};

template <typename Predecessor, typename Successor>
struct _sender {
  struct type;
};
template <typename Predecessor, typename Successor>
using sender = typename _sender<
    remove_cvref_t<Predecessor>,
    remove_cvref_t<Successor>>::type;

template <typename Predecessor, typename Successor>
struct _sender<Predecessor, Successor>::type {
  using sender = type;
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Successor succ_;

  template <typename... Ts>
  using overload_list = type_list<type_list<std::decay_t<Ts>...>>;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types =
      type_list_nested_apply_t<
          sender_value_types_t<Predecessor, concat_type_lists_unique_t, overload_list>,
          Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types =
      typename concat_type_lists_unique_t<
          sender_error_types_t<Predecessor, type_list>,
          sender_error_types_t<Successor, type_list>,
          type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done =
    sender_traits<Predecessor>::sends_done ||
    sender_traits<Successor>::sends_done;

  friend constexpr auto tag_invoke(tag_t<blocking>, const sender& self) noexcept {
    if constexpr (
        blocking_kind::maybe != cblocking<Predecessor>() &&
        blocking_kind::maybe != cblocking<Successor>()) {
      return blocking_kind::constant<
          _via::_blocking_kind(cblocking<Predecessor>(), cblocking<Successor>())>();
    } else {
      return _via::_blocking_kind(blocking(self.pred_), blocking(self.succ_));
    }
  }

  template <typename Receiver>
  auto connect(Receiver&& receiver) && {
    return unifex::connect(
        static_cast<Predecessor&&>(pred_),
        predecessor_receiver<Successor, Receiver>{
            static_cast<Successor&&>(succ_),
            static_cast<Receiver&&>(receiver)});
  }
};
} // namespace _via

namespace _via_cpo {
  inline const struct _fn {
    template (typename Scheduler, typename Sender)
      (requires scheduler<Scheduler> AND sender<Sender>)
    auto operator()(Scheduler&& sched, Sender&& send) const
        noexcept(noexcept(
            _via::sender<Sender, schedule_result_t<Scheduler>>{
                (Sender&&) send, schedule(sched)}))
        -> _via::sender<Sender, schedule_result_t<Scheduler>> {
      return _via::sender<Sender, schedule_result_t<Scheduler>>{
          (Sender&&) send,
          schedule(sched)};
    }
  } via{};
} // namespace _via_cpo

using _via_cpo::via;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
