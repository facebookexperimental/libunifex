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

template <typename InnerOp, typename Receiver, typename... StateFactories>
struct _operation {
  struct type;
};

template <typename InnerOp, typename Receiver, typename... StateFactories>
using operation =  typename _operation<
    InnerOp, Receiver, StateFactories...>::type;

template<typename SuccessorFactory, typename... StateFactory>
struct _sender {
    class type;
};

template<typename SuccessorFactory, typename... StateFactories>
using let_with_sender = typename _sender<SuccessorFactory, StateFactories...>::type;

 template<bool...Bs>
inline constexpr bool and_v = (Bs &&...);

template<typename SuccessorFactory, typename... StateFactories>
class _sender<SuccessorFactory, StateFactories...>::type {
public:
    using InnerOp = std::invoke_result_t<SuccessorFactory, callable_result_t<StateFactories>&...>;

    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = typename InnerOp::template value_types<Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types = typename InnerOp::template error_types<Variant>;

    template<typename StateFactoryTuple, typename SuccessorFactory2>
    explicit type(StateFactoryTuple&& stateFactoryTuple, SuccessorFactory2&& func) :
        stateFactories_((StateFactoryTuple&&)stateFactoryTuple),
        func_((SuccessorFactory2&&)func)
    {}

    template(typename Self, typename Receiver)
        (requires same_as<remove_cvref_t<Self>, type> AND receiver<Receiver>)
    friend auto tag_invoke(tag_t<unifex::connect>, Self&& self, Receiver&& r)
        noexcept(
            (and_v<is_nothrow_callable_v<member_t<Self, StateFactories>> ...> ) &&
            std::is_nothrow_invocable_v<
                member_t<Self, SuccessorFactory>,
                std::invoke_result_t<member_t<Self, StateFactories>>& ...> &&
            is_nothrow_connectable_v<
                callable_result_t<
                    member_t<Self, SuccessorFactory>,
                    std::invoke_result_t<member_t<Self, StateFactories>>& ...>,
                remove_cvref_t<Receiver>>) {
        return operation<SuccessorFactory, Receiver, StateFactories...>(
            static_cast<Self&&>(self).stateFactories_,
            static_cast<Self&&>(self).func_,
            static_cast<Receiver&&>(r));
    }

private:
    UNIFEX_NO_UNIQUE_ADDRESS std::tuple<StateFactories...> stateFactories_;
    UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
};

// Conversion helper to support in-place construction via RVO
template<typename Func>
struct in_place_construction_converter {
    operator callable_result_t<Func>() {
        return func_();
    }
    Func& func_;
};
template<typename T>
auto in_place_construction_helper(T& stateFactory) {
    return in_place_construction_converter<remove_cvref_t<T>>{stateFactory};
}

template<typename SuccessorFactory, typename Receiver, typename... StateFactories>
struct _operation<SuccessorFactory, Receiver, StateFactories...>::type {
    using StateTupleT = std::tuple<remove_cvref_t<callable_result_t<StateFactories>>...>;
    type(std::tuple<StateFactories...>&& stateFactories, SuccessorFactory&& func, Receiver&& r) :
        stateFactories_((std::tuple<StateFactories...>&&)stateFactories),
        func_(static_cast<SuccessorFactory&&>(func)),
        // Construct the tuple of state from the tuple of factories
        // using in-place construction via RVO
        state_(std::apply([](auto&&... stateFactory){
            return StateTupleT{
                in_place_construction_helper(stateFactory)...
            };
        },
        stateFactories_)),
        innerOp_(
              unifex::connect(
                std::apply(func_, state_),
                static_cast<Receiver&&>(r))) {
    }

    void start() & noexcept {
        unifex::start(innerOp_);
    }

    UNIFEX_NO_UNIQUE_ADDRESS std::tuple<StateFactories...> stateFactories_;
    UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
    StateTupleT state_;
    connect_result_t<
        callable_result_t<SuccessorFactory, callable_result_t<StateFactories>&...>,
        remove_cvref_t<Receiver>>
        innerOp_;
};

} // namespace _let_with

namespace _let_with_cpo {

struct _fn {
private:
    template<typename SuccessorFactory, typename... StateFactories>
    auto let_with_builder(std::tuple<StateFactories...>&& stateFactories, SuccessorFactory&& successorFactory) const {
        return _let_with::let_with_sender<std::decay_t<SuccessorFactory>, std::decay_t<StateFactories>...>{
            (std::tuple<StateFactories...>&&)stateFactories, (SuccessorFactory&&)successorFactory};
    }

    template<typename StateFactory, typename SuccessorFactory>
    auto let_with_helper(StateFactory&& stateFactory, SuccessorFactory&& successorFactory) const {
        return std::pair<std::tuple<StateFactory>, SuccessorFactory>(
            std::tuple<StateFactory>{(StateFactory&&)stateFactory},
            (SuccessorFactory&&)successorFactory);
    }

    template<typename Factory, typename... Factories>
    auto let_with_helper(Factory&& factory, Factories&&... factories) const {
        auto p = let_with_helper((Factories&&)factories...);
        return make_pair(
            std::tuple_cat(
                std::make_tuple((Factory&&)factory),
                std::move(p.first)),
            std::move(p.second)
        );
    }

public:
    template<typename... Factories>
    auto operator()(Factories&&... factories) const {
        auto p = let_with_helper((Factories&&)factories...);
        return let_with_builder(std::move(p.first), std::move(p.second));
    }
};
} // namespace _let_with_cpo

inline constexpr _let_with_cpo::_fn let_with{};


} // namespace unifex

#include <unifex/detail/epilogue.hpp>
