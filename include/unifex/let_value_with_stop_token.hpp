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
#include <unifex/execution_policy.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_list.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _let_v_w_stop_tok {

template <typename Receiver>
struct _stop_token_receiver {
  class type;
};

template <typename SuccessorFactory, typename Receiver, typename = void>
struct _stop_token_operation {
  struct type;
};

template <typename SuccessorFactory, typename Receiver>
using operation = typename _stop_token_operation<SuccessorFactory, Receiver, void>::type;

template <typename Receiver>
using stop_token_receiver = typename _stop_token_receiver<Receiver>::type;

template <typename Receiver>
class _stop_token_receiver<Receiver>::type {
public:
  explicit type(inplace_stop_token stop_token, Receiver&& r)
    noexcept(unifex::is_nothrow_move_constructible_v<Receiver>)
    : stop_token_(stop_token)
    , receiver_{(Receiver &&) r} {}

  template(typename... Values)
      (requires receiver_of<Receiver, Values...>)
  void set_value(Values&&... values)
    noexcept(is_nothrow_receiver_of_v<Receiver, Values...>) {
    unifex::set_value(std::move(receiver_), (Values &&) values...);
  }

  template(typename Error)
      (requires receiver<Receiver, Error>)
  void set_error(Error&& error) noexcept {
    unifex::set_error(std::move(receiver_), (Error &&) error);
  }

  void set_done() noexcept {
    unifex::set_done(std::move(receiver_));
  }

  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const type& r) noexcept {
    return r.stop_token_;
  }

  template(typename CPO, typename Self)
    (requires is_receiver_query_cpo_v<CPO> AND same_as<Self,type>)
  friend auto tag_invoke(CPO cpo, const Self& self)
    noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return cpo(self.receiver_);
  }

  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      VisitFunc&& func) noexcept(is_nothrow_callable_v<
                                VisitFunc&,
                                const Receiver&>) {
    func(r.receiver_);
  }

private:
  inplace_stop_token stop_token_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
};

template <typename SuccessorFactory>
struct _stop_token_sender {
  class type;
};

template <typename SuccessorFactory>
using stop_token_sender = typename _stop_token_sender<SuccessorFactory>::type;

template <typename SuccessorFactory>
class _stop_token_sender<SuccessorFactory>::type {
public:
  using inner_sender_t =
      unifex::invoke_result_t<SuccessorFactory, inplace_stop_token>;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = sender_value_types_t<inner_sender_t, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<inner_sender_t, Variant>;

  static constexpr bool sends_done = sender_traits<inner_sender_t>::sends_done;

  template <typename SuccessorFactory2>
  explicit type(SuccessorFactory2&& func) noexcept(
      unifex::is_nothrow_constructible_v<SuccessorFactory, SuccessorFactory2&&>)
    : func_((SuccessorFactory2 &&) func) {}

  template(typename Self, typename Receiver)
    (requires same_as<remove_cvref_t<Self>, type> AND receiver<Receiver>) friend
  auto tag_invoke(tag_t<unifex::connect>, Self&& self, Receiver&& r)
    noexcept(
      is_nothrow_invocable_v<member_t<Self, SuccessorFactory>, inplace_stop_token>
      && is_nothrow_move_constructible_v<Receiver>
      && is_nothrow_constructible_v<
        operation<member_t<Self, SuccessorFactory>, unifex::remove_cvref_t<Receiver>>,
        SuccessorFactory&&,
        Receiver&&>)
  -> operation<
      SuccessorFactory,
      unifex::remove_cvref_t<Receiver>> {
    return operation<
        SuccessorFactory,
        unifex::remove_cvref_t<Receiver>>(
        static_cast<Self&&>(self).func_, static_cast<Receiver&&>(r));
  }

private:
  UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
};

template <typename SuccessorFactory, typename Receiver>
struct _stop_token_operation<
    SuccessorFactory,
    Receiver,
    std::enable_if_t<
        std::is_same_v<stop_token_type_t<Receiver>, inplace_stop_token> ||
        is_stop_never_possible_v<stop_token_type_t<Receiver>>>> {
  struct type {
    using inner_sender_t =
        unifex::invoke_result_t<SuccessorFactory&&, inplace_stop_token>;
    using receiver_t = stop_token_receiver<unifex::remove_cvref_t<Receiver>>;

    static constexpr bool successor_is_nothrow =
        unifex::is_nothrow_invocable_v<SuccessorFactory&&, inplace_stop_token>;
    static constexpr bool inner_receiver_nothrow_constructible = unifex::
        is_nothrow_constructible_v<receiver_t, inplace_stop_token, Receiver&&>;
    static constexpr bool nothrow_connectable =
        unifex::is_nothrow_connectable_v<inner_sender_t, receiver_t>;

  private:
    auto connect_inner_op(
        SuccessorFactory& func,
        inplace_stop_token st,
        Receiver&& r)
    noexcept(
      successor_is_nothrow
      && inner_receiver_nothrow_constructible
      && nothrow_connectable) {
      return unifex::connect(
          func(st), receiver_t(st, (Receiver &&) r));
    }

    SuccessorFactory func_;
    connect_result_t<inner_sender_t, receiver_t> innerOp_;

  public:
    template <typename Receiver2>
    inplace_stop_token get_token(Receiver2& r) noexcept {
      if constexpr (std::is_same_v<
                        stop_token_type_t<Receiver2>,
                        inplace_stop_token>) {
        return get_stop_token(r);
      } else {
        return inplace_stop_token{};
      }
    }

    template <typename SuccessorFactory2, typename Receiver2>
    type(SuccessorFactory2&& func, Receiver2&& r) noexcept(
        is_nothrow_constructible_v<SuccessorFactory, SuccessorFactory2&&>
        && is_nothrow_constructible_v<Receiver, Receiver2&&>
        && noexcept(
            connect_inner_op(
              func_,
              std::declval<inplace_stop_token>(),
              (Receiver2&&)r)))
      : func_((SuccessorFactory2 &&) func)
      , innerOp_(connect_inner_op(
            func_, get_token(r), (Receiver2 &&) r)) {}

    void start() noexcept {
      unifex::start(innerOp_);
    }
  };
};

template <typename SuccessorFactory, typename Receiver, typename AlwaysVoid>
struct _stop_token_operation<SuccessorFactory, Receiver, AlwaysVoid>::type {
  using inner_sender_t =
      unifex::invoke_result_t<SuccessorFactory&&, inplace_stop_token>;
  using receiver_t = stop_token_receiver<unifex::remove_cvref_t<Receiver>>;

  static constexpr bool successor_is_nothrow =
      unifex::is_nothrow_invocable_v<SuccessorFactory&&, inplace_stop_token>;
  static constexpr bool inner_receiver_nothrow_constructible = unifex::
      is_nothrow_constructible_v<receiver_t, inplace_stop_token, Receiver&&>;
  static constexpr bool nothrow_connectable =
      unifex::is_nothrow_connectable_v<inner_sender_t, receiver_t>;

private:
  auto connect_inner_op(
      SuccessorFactory& func,
      inplace_stop_token st,
      Receiver&& r)
    noexcept(
      successor_is_nothrow
      && inner_receiver_nothrow_constructible
      && nothrow_connectable) {
    return unifex::connect(
        func(st), receiver_t(st, (Receiver &&) r));
  }

  SuccessorFactory func_;
  stop_token_type_t<Receiver> receiver_token_;
  inplace_stop_source stop_source_;
  connect_result_t<inner_sender_t, receiver_t> innerOp_;
  bool stop_callback_needs_destruction_{false};
  using stop_callback =
      typename stop_token_type_t<Receiver>::template callback_type<
          detail::forward_stop_request_to_inplace_stop_source>;
  unifex::manual_lifetime<stop_callback> callback;

public:
  template <typename SuccessorFactory2, typename Receiver2>
  type(SuccessorFactory2&& func, Receiver2&& r) noexcept(
      is_nothrow_constructible_v<SuccessorFactory, SuccessorFactory2&&>
      && is_nothrow_constructible_v<Receiver, Receiver2>
      && noexcept(
          connect_inner_op(
          func_,
          std::declval<inplace_stop_token>(),
          (Receiver2&&)r)))
    : func_((SuccessorFactory2 &&) func)
    , receiver_token_(get_stop_token(r))
    , innerOp_(connect_inner_op(
          func_,
          stop_source_.get_token(),
          (Receiver2 &&) r)) {}

  ~type() {
    if (stop_callback_needs_destruction_) {
      callback.destruct();
    }
  }

  void start() noexcept {
    callback.construct_with([this]() {
      return stop_callback{std::move(receiver_token_), stop_source_};
    });
    stop_callback_needs_destruction_ = true;
    unifex::start(innerOp_);
  }
};

namespace _cpo {
struct _fn {
  template <typename SuccessorFactory>
  auto operator()(SuccessorFactory&& factory) const
      noexcept(unifex::is_nothrow_constructible_v<
               std::decay_t<SuccessorFactory>,
               SuccessorFactory>)
          -> stop_token_sender<std::decay_t<SuccessorFactory>> {
    return stop_token_sender<std::decay_t<SuccessorFactory>>{
        (SuccessorFactory &&) factory};
  }
};
}  // namespace _cpo
}  // namespace _let_v_w_stop_tok

inline constexpr _let_v_w_stop_tok::_cpo::_fn let_value_with_stop_token{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
