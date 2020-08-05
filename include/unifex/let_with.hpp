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
#include <unifex/just.hpp>
#include <unifex/let.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/execution_policy.hpp>
#include <unifex/type_list.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _let_with {

template<typename Operation, typename Receiver>
struct _let_with_receiver {
    class type;
};

template<typename StateFactory, typename InnerOp, typename Receiver>
struct _let_with_operation {
  struct type;
};

template <typename StateFactory, typename InnerOp, typename Receiver>
using operation =  typename _let_with_operation<
    StateFactory, InnerOp, remove_cvref_t<Receiver>>::type;


template<typename Operation, typename Receiver>
using let_with_receiver = typename _let_with_receiver<Operation, Receiver>::type;

template<typename Operation, typename Receiver>
class _let_with_receiver<Operation, Receiver>::type {
public:
    explicit type(Operation& op, Receiver&& r) :
        op_(op), receiver_{std::forward<Receiver>(r)}
    {}

    template(typename... Values)
        (requires is_value_receiver_v<Receiver, Values...>)
    void set_value(Values&&... values) noexcept(is_nothrow_value_receiver_v<Receiver, Values...>) {
        unifex::set_value(std::move(receiver_), (Values&&)values...);
    }

    template(typename Error)
        (requires is_error_receiver_v<Receiver, Error>)
    void set_error(Error&& error) noexcept {
        unifex::set_error(std::move(receiver_), (Error&&)error);
    }

    template(typename R = Receiver)
        (requires is_done_receiver_v<R>)
    void set_done() noexcept {
        unifex::set_done(std::move(receiver_));
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

template<typename StateFactory, typename SuccessorFactory>
struct _let_with_sender {
    class type;
};

template<typename StateFactory, typename SuccessorFactory>
using let_with_sender = typename _let_with_sender<StateFactory, SuccessorFactory>::type;

template<typename StateFactory, typename SuccessorFactory>
class _let_with_sender<StateFactory, SuccessorFactory>::type {
public:
    using InnerOp = std::invoke_result_t<SuccessorFactory, std::invoke_result_t<StateFactory>&>;

    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = typename InnerOp::template value_types<Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types = typename InnerOp::template error_types<Variant>;

    template<typename StateFactory2, typename SuccessorFactory2>
    explicit type(StateFactory2&& state_factory, SuccessorFactory2&& func) :
        state_factory_((StateFactory2&&)state_factory),
        func_((SuccessorFactory2&&)func)
    {}

    template(typename Self, typename Receiver)
        (requires same_as<remove_cvref_t<Self>, type>)
    friend auto tag_invoke(tag_t<unifex::connect>, Self&& self, Receiver&& r)
        noexcept(
            std::is_nothrow_invocable_v<
                member_t<Self, SuccessorFactory>,
                std::invoke_result_t<StateFactory>&> &&
            std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>) {

        return operation<
                member_t<Self, StateFactory>, member_t<Self, SuccessorFactory>, Receiver>(
            static_cast<Self&&>(self).state_factory_,
            static_cast<Self&&>(self).func_,
            static_cast<Receiver&&>(r));
    }

private:
    UNIFEX_NO_UNIQUE_ADDRESS StateFactory state_factory_;
    UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
};


template<typename StateFactory, typename SuccessorFactory, typename Receiver>
struct _let_with_operation<StateFactory, SuccessorFactory, Receiver>::type {
    type(StateFactory&& state_factory, SuccessorFactory&& func, Receiver&& r) :
        state_{state_factory()},
        innerOp_(
              unifex::connect(
                static_cast<SuccessorFactory&&>(func)(state_),
                let_with_receiver<
                        operation<StateFactory, SuccessorFactory, Receiver>,
                        remove_cvref_t<Receiver>>{
                    *this,
                    static_cast<Receiver&&>(r)})) {
    }

    void start() noexcept {
        unifex::start(innerOp_);
    }

    std::invoke_result_t<StateFactory> state_;
    connect_result_t<
        std::invoke_result_t<SuccessorFactory, std::invoke_result_t<StateFactory>&>,
        let_with_receiver<
            operation<StateFactory, SuccessorFactory, Receiver>,
            remove_cvref_t<Receiver>>>
        innerOp_;
};

struct _fn {

    template<typename StateFactory, typename SuccessorFactory>
    auto operator()(StateFactory&& state_factory, SuccessorFactory&& successor_factory) const
        noexcept(std::is_nothrow_constructible_v<remove_cvref_t<SuccessorFactory>, SuccessorFactory> &&
                 std::is_nothrow_constructible_v<remove_cvref_t<StateFactory>, StateFactory>)
        -> let_with_sender<remove_cvref_t<StateFactory>, remove_cvref_t<SuccessorFactory>> {
        return let_with_sender<remove_cvref_t<StateFactory>, remove_cvref_t<SuccessorFactory>>{
            (StateFactory&&)state_factory, (SuccessorFactory&&)successor_factory};
    }
};

} // namespace _let_with

inline constexpr _let_with::_fn let_with{};


} // namespace unifex

#include <unifex/detail/epilogue.hpp>
