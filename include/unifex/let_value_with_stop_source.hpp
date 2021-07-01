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
#include <unifex/fused_stop_source.hpp>
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/execution_policy.hpp>
#include <unifex/type_list.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _let_v_w_stop_src {

template<typename Operation, typename Receiver>
struct _stop_source_receiver {
    class type;
};

template<typename InnerOp, typename Receiver>
struct _stop_source_operation {
  struct type;
};

template <typename InnerOp, typename Receiver>
using operation =  typename _stop_source_operation<InnerOp, remove_cvref_t<Receiver>>::type;

template<typename Operation, typename Receiver>
using stop_source_receiver = typename _stop_source_receiver<Operation, Receiver>::type;

template<typename Operation, typename Receiver>
class _stop_source_receiver<Operation, Receiver>::type {
public:
    explicit type(Operation& op, Receiver&& r) :
        op_(op), receiver_{std::forward<Receiver>(r)}
    {}

    template(typename... Values)
        (requires receiver_of<Receiver, Values...>)
    void set_value(Values&&... values) noexcept(is_nothrow_receiver_of_v<Receiver, Values...>) {
        op_.stopSource_.deregister_callbacks();
        unifex::set_value(std::move(receiver_), (Values&&)values...);
    }

    template(typename Error)
        (requires receiver<Receiver, Error>)
    void set_error(Error&& error) noexcept {
        op_.stopSource_.deregister_callbacks();
        unifex::set_error(std::move(receiver_), (Error&&)error);
    }

    void set_done() noexcept {
        op_.stopSource_.deregister_callbacks();
        unifex::set_done(std::move(receiver_));
    }

    friend inplace_stop_token tag_invoke(tag_t<get_stop_token>, const type& r) noexcept {
        return r.op_.stopSource_.get_token();
    }

    template(typename CPO, typename Self)
        (requires
            is_receiver_query_cpo_v<CPO> AND
            same_as<Self, type>)
    friend auto tag_invoke(CPO cpo, const Self& self)
        noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
        -> callable_result_t<CPO, const Receiver&> {
        return cpo(self.receiver_);
    }

private:
    UNIFEX_NO_UNIQUE_ADDRESS Operation& op_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
};

template<typename SuccessorFactory>
struct _stop_source_sender {
    class type;
};

template<typename SuccessorFactory>
using stop_source_sender = typename _stop_source_sender<SuccessorFactory>::type;

template<typename SuccessorFactory>
class _stop_source_sender<SuccessorFactory>::type {
    template<typename... Values>
    using additional_arguments = type_list<type_list<Values...>>;

public:
    using InnerOp = std::invoke_result_t<SuccessorFactory, inplace_stop_source&>;

    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = sender_value_types_t<InnerOp, Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types = sender_error_types_t<InnerOp, Variant>;

    static constexpr bool sends_done = sender_traits<InnerOp>::sends_done;

    template<typename SuccessorFactory2>
    explicit type(SuccessorFactory2&& func) : func_((SuccessorFactory2&&)func)
    {}

    template(typename Self, typename Receiver)
        (requires same_as<remove_cvref_t<Self>, type> AND receiver<Receiver>)
    friend auto tag_invoke(tag_t<unifex::connect>, Self&& self, Receiver&& r)
        noexcept(
            std::is_nothrow_invocable_v<member_t<Self, SuccessorFactory>, inplace_stop_source&> &&
            std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>)
        -> operation<member_t<Self, SuccessorFactory>, Receiver> {
        return operation<member_t<Self, SuccessorFactory>, Receiver>(
            static_cast<Self&&>(self).func_, static_cast<Receiver&&>(r));
    }

private:
    UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
};

template <typename SuccessorFactory, typename Receiver>
struct _stop_source_operation<SuccessorFactory, Receiver>::type {
  using stop_token_type = stop_token_type_t<Receiver>;
  using stop_source_type = fused_stop_source<stop_token_type>;
  using receiver_t = stop_source_receiver<
      operation<SuccessorFactory, Receiver>,
      remove_cvref_t<Receiver>>;
  using inner_sender_t =
      std::invoke_result_t<SuccessorFactory&&, stop_source_type&>;

private:
  static constexpr bool successor_is_nothrow =
      std::is_nothrow_invocable_v<SuccessorFactory&&, stop_source_type>;
  static constexpr bool inner_receiver_nothrow_constructible =
      std::is_nothrow_constructible_v<receiver_t, type*, Receiver&&>;
  static constexpr bool nothrow_connectable =
      is_nothrow_connectable_v<inner_sender_t, receiver_t>;

  auto connect_inner_op(SuccessorFactory& func, Receiver&& r) noexcept(
      successor_is_nothrow && inner_receiver_nothrow_constructible &&
          nothrow_connectable) {
    return unifex::connect(
        static_cast<SuccessorFactory&&>(func)(stopSource_),
        receiver_t{*this, static_cast<Receiver&&>(r)});
  }

public:
  template <typename SuccessorFactory2, typename Receiver2>
  type(SuccessorFactory2&& func, Receiver2&& r) noexcept(
      std::is_nothrow_constructible_v<SuccessorFactory, SuccessorFactory2>&&
          std::is_nothrow_constructible_v<Receiver, Receiver2>&& noexcept(
              connect_inner_op(func, (Receiver2 &&) r)))
    : func_{(SuccessorFactory2 &&) func}
    , receiverToken_(get_stop_token(r))
    , innerOp_(connect_inner_op(func_, (Receiver2 &&) r)) {}

  UNIFEX_NO_UNIQUE_ADDRESS stop_source_type stopSource_;
  UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
  UNIFEX_NO_UNIQUE_ADDRESS stop_token_type receiverToken_;
  UNIFEX_NO_UNIQUE_ADDRESS connect_result_t<
      callable_result_t<SuccessorFactory, stop_source_type&>,
      receiver_t>
      innerOp_;

  void start() noexcept {
    stopSource_.register_callbacks(receiverToken_);
    unifex::start(innerOp_);
  }
};

namespace _cpo {
struct _fn {
    template<typename SuccessorFactory>
    auto operator()(SuccessorFactory&& factory) const
        noexcept(std::is_nothrow_constructible_v<std::decay_t<SuccessorFactory>, SuccessorFactory>)
        -> stop_source_sender<std::decay_t<SuccessorFactory>> {
        return stop_source_sender<std::decay_t<SuccessorFactory>>{(SuccessorFactory&&)factory};
    }
};
} // namespace _cpo
} // namespace _let_v_w_stop_src

inline constexpr _let_v_w_stop_src::_cpo::_fn let_value_with_stop_source{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
