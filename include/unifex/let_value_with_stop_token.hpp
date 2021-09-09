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
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/execution_policy.hpp>
#include <unifex/type_list.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _let_v_w_stop_tok {

template<typename Operation, typename Receiver>
struct _stop_token_receiver {
    class type;
};

template<typename InnerOp, typename Receiver>
struct _stop_token_operation {
  struct type;
};

template <typename InnerOp, typename Receiver>
using operation =  typename _stop_token_operation<InnerOp, remove_cvref_t<Receiver>>::type;

template<typename Operation, typename Receiver>
using stop_token_receiver = typename _stop_token_receiver<Operation, Receiver>::type;

template<typename Operation, typename Receiver>
class _stop_token_receiver<Operation, Receiver>::type {
public:
    explicit type(Operation& op, Receiver&& r) noexcept(std::is_nothrow_move_constructible_v<Receiver>) :
        op_(&op), receiver_{(Receiver &&)r}
    {}

    template(typename... Values)
        (requires receiver_of<Receiver, Values...>)
    void set_value(Values&&... values) noexcept(is_nothrow_receiver_of_v<Receiver, Values...>) {
        unifex::set_value(std::move(receiver_), (Values&&)values...);
    }

    template(typename Error)
        (requires receiver<Receiver, Error>)
    void set_error(Error&& error) noexcept {
        unifex::set_error(std::move(receiver_), (Error&&)error);
    }

    void set_done() noexcept {
        unifex::set_done(std::move(receiver_));
    }

    friend inplace_stop_token tag_invoke(tag_t<get_stop_token>, const type& r) noexcept {
        return r.op_->stop_token_;
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
    Operation* op_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
};

template<typename SuccessorFactory>
struct _stop_token_sender {
    class type;
};

template<typename SuccessorFactory>
using stop_token_sender = typename _stop_token_sender<SuccessorFactory>::type;

template<typename SuccessorFactory>
class _stop_token_sender<SuccessorFactory>::type {
    template<typename... Values>
    using additional_arguments = type_list<type_list<Values...>>;

public:
    using InnerOp = std::invoke_result_t<SuccessorFactory, inplace_stop_token&>;

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
            std::is_nothrow_invocable_v<member_t<Self, SuccessorFactory>, inplace_stop_token&> &&
            std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>)
        -> operation<member_t<Self, SuccessorFactory>, Receiver> {
        return operation<member_t<Self, SuccessorFactory>, Receiver>(
            static_cast<Self&&>(self).func_, static_cast<Receiver&&>(r));
    }

private:
    UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
};

template<typename SuccessorFactory, typename Receiver>
struct _stop_token_operation<SuccessorFactory, Receiver>::type {

    template <typename SuccessorFactory2, typename Receiver2>
    type(SuccessorFactory2&& func, Receiver2&& r) :
        func_{(SuccessorFactory2&&)func},
        stop_token_{stop_token_adapter_.subscribe(get_stop_token(r))},
        innerOp_(
              unifex::connect(
                ((SuccessorFactory&&)func_)(stop_token_),
                stop_token_receiver<operation<SuccessorFactory, Receiver>, remove_cvref_t<Receiver2>>{
                    *this,
                    static_cast<Receiver2&&>(r)})) {
    }

    void start() noexcept {
        unifex::start(innerOp_);
    }

    UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
    inplace_stop_token_adapter<stop_token_type_t<Receiver>> stop_token_adapter_;
    inplace_stop_token stop_token_;
    connect_result_t<
        callable_result_t<SuccessorFactory, unifex::inplace_stop_token&>,
        stop_token_receiver<operation<SuccessorFactory, Receiver>, remove_cvref_t<Receiver>>>
        innerOp_;
};

namespace _cpo {
struct _fn {
    template<typename SuccessorFactory>
    auto operator()(SuccessorFactory&& factory) const
        noexcept(std::is_nothrow_constructible_v<std::decay_t<SuccessorFactory>, SuccessorFactory>)
        -> stop_token_sender<std::decay_t<SuccessorFactory>> {
        return stop_token_sender<std::decay_t<SuccessorFactory>>{(SuccessorFactory&&)factory};
    }
};
} // namespace _cpo
} // namespace _let_v_w_stop_tok

inline constexpr _let_v_w_stop_tok::_cpo::_fn let_value_with_stop_token{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
