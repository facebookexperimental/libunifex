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
#include <unifex/fused_stop_source.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_list.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _let_v_w_stop_tok {

template <typename Operation, typename Receiver>
struct _stop_token_receiver {
  class type;
};

template <typename SuccessorFactory, typename Receiver, typename = void>
struct _stop_token_operation {
  struct type;
};

template <typename SuccessorFactory, typename Receiver>
using operation =
    typename _stop_token_operation<SuccessorFactory, Receiver, void>::type;

template <typename Operation, typename Receiver>
using stop_token_receiver =
    typename _stop_token_receiver<Operation, Receiver>::type;

template <typename Operation, typename Receiver>
class _stop_token_receiver<Operation, Receiver>::type {
public:
  template <typename Receiver2>
  explicit type(
      Operation* op,
      inplace_stop_token stop_token,
      Receiver2&&
          r) noexcept(std::is_nothrow_constructible_v<Receiver, Receiver2>)
    : op_(op)
    , stop_token_(stop_token)
    , receiver_{static_cast<Receiver2&&>(r)} {}

  template(typename... Values)                     //
      (requires receiver_of<Receiver, Values...>)  //
      void set_value(Values&&... values) noexcept(
          is_nothrow_receiver_of_v<Receiver, Values...>) {
    cleanup();
    unifex::set_value(std::move(receiver_), (Values &&) values...);
  }

  template(typename Error)                  //
      (requires receiver<Receiver, Error>)  //
      void set_error(Error&& error) noexcept {
    cleanup();
    unifex::set_error(std::move(receiver_), (Error &&) error);
  }

  void set_done() noexcept {
    cleanup();
    unifex::set_done(std::move(receiver_));
  }

  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const type& r) noexcept {
    return r.stop_token_;
  }

  template(typename CPO, typename Self)       //
      (requires is_receiver_query_cpo_v<CPO>  //
           AND same_as<Self, type>)           //
      friend auto tag_invoke(CPO cpo, const Self& self) noexcept(
          is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return cpo(self.receiver_);
  }

  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      VisitFunc&&
          func) noexcept(is_nothrow_callable_v<VisitFunc&, const Receiver&>) {
    func(r.receiver_);
  }

private:
  Operation* op_;
  inplace_stop_token stop_token_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  void cleanup() noexcept { op_->cleanup(); }
};

template <typename SuccessorFactory>
struct _stop_token_sender {
  class type;
};

template <typename SuccessorFactory>
using stop_token_sender = typename _stop_token_sender<SuccessorFactory>::type;

template <typename SuccessorFactory>
class _stop_token_sender<SuccessorFactory>::type {
  static_assert(
      !std::is_reference_v<SuccessorFactory>,
      "SuccessorFactory should be a value, not a reference");

public:
  using inner_sender_t =
      std::invoke_result_t<SuccessorFactory&, inplace_stop_token>;

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
      std::is_nothrow_constructible_v<SuccessorFactory, SuccessorFactory2&&>)
    : func_((SuccessorFactory2 &&) func) {}

  template(typename Self, typename Receiver)                                 //
      (requires same_as<remove_cvref_t<Self>, type> AND receiver<Receiver>)  //
      friend auto tag_invoke(
          tag_t<unifex::connect>,
          Self&& self,
          Receiver&& r) noexcept(std::
                                     is_nothrow_constructible_v<
                                         operation<
                                             SuccessorFactory,
                                             unifex::remove_cvref_t<Receiver>>,
                                         member_t<Self, SuccessorFactory>,
                                         Receiver>)
          -> operation<SuccessorFactory, unifex::remove_cvref_t<Receiver>> {
    return operation<SuccessorFactory, unifex::remove_cvref_t<Receiver>>(
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
  static_assert(
      !std::is_reference_v<SuccessorFactory>,
      "SuccessorFactory should be a value, not a reference");

  static_assert(
      !std::is_reference_v<Receiver>,
      "Receiver should be a value, not a reference");

  struct type {
    using inner_sender_t =
        std::invoke_result_t<SuccessorFactory&, inplace_stop_token>;
    using receiver_t =
        stop_token_receiver<operation<SuccessorFactory, Receiver>, Receiver>;

    static constexpr bool successor_is_nothrow =
        std::is_nothrow_invocable_v<SuccessorFactory&, inplace_stop_token>;
    template <typename Receiver2>
    static constexpr bool inner_receiver_nothrow_constructible =
        std::is_nothrow_constructible_v<
            receiver_t,
            type*,
            inplace_stop_token,
            Receiver2>;
    static constexpr bool nothrow_connectable =
        unifex::is_nothrow_connectable_v<inner_sender_t, receiver_t>;

  private:
    template <typename Receiver2>
    auto connect_inner_op(
        SuccessorFactory& func,
        inplace_stop_token st,
        // we need to take r by reference to avoid problems at the call site
        // related to unsequenced function argument evaluation
        Receiver2&& r) noexcept(successor_is_nothrow&&
                                    inner_receiver_nothrow_constructible<
                                        Receiver2>&& nothrow_connectable) {
      return unifex::connect(
          func(st), receiver_t{this, st, static_cast<Receiver2&&>(r)});
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
    explicit type(SuccessorFactory2&& func, Receiver2&& r) noexcept(
        std::is_nothrow_constructible_v<SuccessorFactory, SuccessorFactory2>  //
            && std::is_nothrow_constructible_v<Receiver, Receiver2>           //
                && noexcept(connect_inner_op(
                    func_, inplace_stop_token{}, static_cast<Receiver2&&>(r))))
      : func_((SuccessorFactory2 &&) func)
      , innerOp_(connect_inner_op(
            func_, get_token(r), static_cast<Receiver2&&>(r))) {}

    type(type&&) = delete;

    ~type() = default;

    void start() noexcept { unifex::start(innerOp_); }

    void cleanup() noexcept {}
  };
};

template <typename SuccessorFactory, typename Receiver, typename AlwaysVoid>
struct _stop_token_operation<SuccessorFactory, Receiver, AlwaysVoid>::type {
  static_assert(
      !std::is_reference_v<SuccessorFactory>,
      "SuccessorFactory should be a value, not a reference");

  static_assert(
      !std::is_reference_v<Receiver>,
      "Receiver should be a value, not a reference");

  using inner_sender_t =
      std::invoke_result_t<SuccessorFactory&, inplace_stop_token>;
  using receiver_t =
      stop_token_receiver<operation<SuccessorFactory, Receiver>, Receiver>;

  static constexpr bool successor_is_nothrow =
      std::is_nothrow_invocable_v<SuccessorFactory&, inplace_stop_token>;
  template <typename Receiver2>
  static constexpr bool inner_receiver_nothrow_constructible =
      std::is_nothrow_constructible_v<
          receiver_t,
          type*,
          inplace_stop_token,
          Receiver2>;
  static constexpr bool nothrow_connectable =
      unifex::is_nothrow_connectable_v<inner_sender_t, receiver_t>;

private:
  template <typename Receiver2>
  auto connect_inner_op(
      SuccessorFactory& func,
      inplace_stop_token st,
      Receiver2&& r) noexcept(successor_is_nothrow&&
                                  inner_receiver_nothrow_constructible<
                                      Receiver2>&& nothrow_connectable) {
    return unifex::connect(
        func(st), receiver_t{this, st, static_cast<Receiver2&&>(r)});
  }

  SuccessorFactory func_;
  using stop_token_type = stop_token_type_t<Receiver>;
  stop_token_type receiverToken_;
  fused_stop_source<stop_token_type> stopSource_;
  connect_result_t<inner_sender_t, receiver_t> innerOp_;

public:
  template <typename SuccessorFactory2, typename Receiver2>
  explicit type(SuccessorFactory2&& func, Receiver2&& r) noexcept(
      std::is_nothrow_constructible_v<SuccessorFactory, SuccessorFactory2>  //
          && std::is_nothrow_constructible_v<Receiver, Receiver2>           //
              && noexcept(connect_inner_op(
                  func_, inplace_stop_token{}, static_cast<Receiver2&&>(r))))
    : func_((SuccessorFactory2 &&) func)
    , receiverToken_(get_stop_token(r))
    , innerOp_(connect_inner_op(
          func_, stopSource_.get_token(), static_cast<Receiver2&&>(r))) {}

  type(type&&) = delete;

  ~type() = default;

  void start() noexcept {
    stopSource_.register_callbacks(receiverToken_);
    unifex::start(innerOp_);
  }

  void cleanup() noexcept { stopSource_.deregister_callbacks(); }
};

namespace _cpo {
struct _fn {
  template <typename SuccessorFactory>
  auto operator()(SuccessorFactory&& factory) const
      noexcept(std::is_nothrow_constructible_v<
               stop_token_sender<std::decay_t<SuccessorFactory>>,
               SuccessorFactory>)
          -> stop_token_sender<std::decay_t<SuccessorFactory>> {
    return stop_token_sender<std::decay_t<SuccessorFactory>>{
        static_cast<SuccessorFactory&&>(factory)};
  }
};
}  // namespace _cpo
}  // namespace _let_v_w_stop_tok

inline constexpr _let_v_w_stop_tok::_cpo::_fn let_value_with_stop_token{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
