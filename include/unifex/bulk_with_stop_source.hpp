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
#include <unifex/inplace_stop_token.hpp>
#include <unifex/execution_policy.hpp>
#include <unifex/get_execution_policy.hpp>
#include <unifex/type_list.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _bulk_with_stop_source {

template<typename Operation, typename Receiver>
struct _stop_source_receiver {
    class type;
};

template<typename Pred, typename Receiver>
struct _stop_source_operation {
  struct type;
};

template <typename Predecessor, typename Receiver>
using operation =  typename _stop_source_operation<Predecessor, remove_cvref_t<Receiver>>::type;


template<typename Operation, typename Receiver>
using stop_source_receiver = typename _stop_source_receiver<Operation, Receiver>::type;

template<typename Operation, typename Receiver>
class _stop_source_receiver<Operation, Receiver>::type {
public:
    explicit type(Operation& op, Receiver&& r) :
        op_(op), receiver_{std::forward<Receiver>(r)}
    {}

    template<typename... Values>
    void set_next(Values&&... values) &
        noexcept(is_nothrow_next_receiver_v<Receiver, Values...,  unifex::inplace_stop_source&>) {

        // Forward, injecting stop_source
        unifex::set_next(receiver_, std::forward<Values>(values)..., op_.stop_source_);
    }

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
        (requires is_done_receiver_v<Receiver>)
    void set_done() noexcept {
        // If the local stop token is set and the incoming one is not,
        // switch back to set_value
        if(op_.stop_source_.stop_requested() && !unifex::get_stop_token(receiver_).stop_requested()) {
            unifex::set_value(std::move(receiver_));
        } else {
            unifex::set_done(std::move(receiver_));
        }
    }

    friend auto tag_invoke(tag_t<get_execution_policy>, const type& r) noexcept {
        return get_execution_policy(r.receiver_);
    }

    friend auto tag_invoke(tag_t<get_stop_token>, const type& r) noexcept {
        return r.op_.stop_source_.get_token();
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

template<typename Source>
struct _stop_source_sender {
    class type;
};

template<typename Source>
using stop_source_sender = typename _stop_source_sender<Source>::type;


template <typename Result>
struct result_overload {
using type = type_list<Result>;
};
template<typename Source>
class _stop_source_sender<Source>::type {
    template<typename... Values>
    using additional_arguments = type_list<type_list<Values..., inplace_stop_source&>>;

public:
    using additional_types = type_list<inplace_stop_source>;
    template<
        template<typename...> class Variant,
        template<typename...> class Tuple>
    using next_types = type_list_nested_apply_t<
        typename Source::template next_types<concat_type_lists_unique_t, additional_arguments>,
        Variant,
        Tuple>;

    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = typename Source::template value_types<Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types = typename Source::template error_types<Variant>;

    static constexpr bool sends_done = Source::sends_done;

    template<typename Source2>
    explicit type(Source2&& source) : source_((Source2&&)source)
    {}

    template(typename Self, typename Receiver)
        (requires
            same_as<remove_cvref_t<Self>, type> AND
            sender_to<member_t<Self, Source>, stop_source_receiver<operation<Source, Receiver>, remove_cvref_t<Receiver>>>)
    friend auto tag_invoke(tag_t<unifex::connect>, Self&& self, Receiver&& r)
        noexcept(
            std::is_nothrow_constructible_v<Source, member_t<Self, Source>> &&
            std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>) {

        return operation<Source, Receiver>(
            static_cast<Self&&>(self).source_, std::forward<Receiver>(r));
    }

private:
    UNIFEX_NO_UNIQUE_ADDRESS Source source_;
};

template<typename Pred, typename Receiver, typename Token>
struct _stop_source_operation_callback;
template<typename Pred, typename Receiver, typename Token>
struct _stop_source_operation_callback {
    _stop_source_operation_callback(operation<Pred, Receiver>& op, Token&& token) :
        op_{op}, token_{token} {}
    void operator()() {
        op_.stop_source_.request_stop();
    }
    operation<Pred, Receiver>& op_;
    Token token_;
};
template<typename Pred, typename Receiver>
struct _stop_source_operation_callback<Pred, Receiver, unstoppable_token> {
    _stop_source_operation_callback(operation<Pred, Receiver>& /*op*/, unstoppable_token&& /*token*/)  {}
    void operator()() {}
};

template<typename Pred, typename Receiver>
struct _stop_source_operation<Pred, Receiver>::type {
    type(Pred&& pred, Receiver&& r) :
        stop_callback_(*this, unifex::get_stop_token(r)),
        predOp_(
            unifex::connect(
                (Pred&&)(pred),
                stop_source_receiver<operation<Pred, Receiver>, remove_cvref_t<Receiver>>{
                    *this,
                    static_cast<Receiver&&>(r)})),
        stop_source_{} {
    }

    void start() noexcept {
        unifex::start(predOp_);
    }

    UNIFEX_NO_UNIQUE_ADDRESS _stop_source_operation_callback<
            Pred, Receiver, remove_cvref_t<decltype(unifex::get_stop_token(std::declval<Receiver>()))>>
        stop_callback_;
    connect_result_t<
        Pred,
        stop_source_receiver<operation<Pred, Receiver>, remove_cvref_t<Receiver>>>
        predOp_;
    UNIFEX_NO_UNIQUE_ADDRESS unifex::inplace_stop_source stop_source_;
};


struct _fn {

    template(typename Source)
        (requires typed_bulk_sender<Source>)
    auto operator()(Source&& s) const
        noexcept(std::is_nothrow_constructible_v<remove_cvref_t<Source>, Source>)
        -> stop_source_sender<remove_cvref_t<Source>> {
        return stop_source_sender<remove_cvref_t<Source>>{(Source&&)s};
    }
};

} // namespace _bulk_with_stop_source

inline constexpr _bulk_with_stop_source::_fn bulk_with_stop_source{};




///////// TODO: Make the below work then delete above

namespace _let_with_stop_source {

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

    template<typename... Values>
    void set_next(Values&&... values) &
        noexcept(is_nothrow_next_receiver_v<Receiver, Values...,  unifex::inplace_stop_source&>) {

        // Forward, injecting stop_source
        unifex::set_next(receiver_, std::forward<Values>(values)..., op_.stop_source_);
    }

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
        (requires is_done_receiver_v<Receiver>)
    void set_done() noexcept {
        // If the local stop token is set and the incoming one is not,
        // switch back to set_value
        if(op_.stop_source_.stop_requested() && !unifex::get_stop_token(receiver_).stop_requested()) {
            unifex::set_value(std::move(receiver_));
        } else {
            unifex::set_done(std::move(receiver_));
        }
    }

    friend auto tag_invoke(tag_t<get_execution_policy>, const type& r) noexcept {
        return get_execution_policy(r.receiver_);
    }

    friend auto tag_invoke(tag_t<get_stop_token>, const type& r) noexcept {
        return r.op_.stop_source_.get_token();
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


template <typename Result>
struct result_overload {
using type = type_list<Result>;
};
template<typename SuccessorFactory>
class _stop_source_sender<SuccessorFactory>::type {
    template<typename... Values>
    using additional_arguments = type_list<type_list<Values...>>;

public:
    using InnerOp = std::invoke_result_t<SuccessorFactory, inplace_stop_source&>;

    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = typename InnerOp::template value_types<Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types = typename InnerOp::template error_types<Variant>;

    static constexpr bool sends_done = InnerOp::sends_done;

    template<typename SuccessorFactory2>
    explicit type(SuccessorFactory2&& func) : func_((SuccessorFactory2&&)func)
    {}

    template(typename Self, typename Receiver)
        (requires same_as<remove_cvref_t<Self>, type>)
    friend auto tag_invoke(tag_t<unifex::connect>, Self&& self, Receiver&& r)
        noexcept(
            std::is_nothrow_constructible_v<SuccessorFactory, member_t<Self, SuccessorFactory>> &&
            std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>) {

        return operation<SuccessorFactory, Receiver>(
            static_cast<Self&&>(self).func_, std::forward<Receiver>(r));
    }

private:
    UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
};

template<typename SuccessorFactory, typename Receiver, typename Token>
struct _stop_source_operation_callback;
template<typename SuccessorFactory, typename Receiver, typename Token>
struct _stop_source_operation_callback {
    _stop_source_operation_callback(operation<SuccessorFactory, Receiver>& op, Token&& token) :
        op_{op}, token_{token} {}
    void operator()() {
        op_.stop_source_.request_stop();
    }
    operation<SuccessorFactory, Receiver>& op_;
    Token token_;
};
template<typename SuccessorFactory, typename Receiver>
struct _stop_source_operation_callback<SuccessorFactory, Receiver, unstoppable_token> {
    _stop_source_operation_callback(operation<SuccessorFactory, Receiver>& /*op*/, unstoppable_token&& /*token*/)  {}
    void operator()() {}
};

template<typename SuccessorFactory, typename Receiver>
struct _stop_source_operation<SuccessorFactory, Receiver>::type {
    type(SuccessorFactory&& func, Receiver&& r) :
        stop_source_{},
        stop_callback_(*this, unifex::get_stop_token(r)),
        innerOp_(
            unifex::connect(
                func(stop_source_),
                stop_source_receiver<operation<SuccessorFactory, Receiver>, remove_cvref_t<Receiver>>{
                    *this,
                    static_cast<Receiver&&>(r)})) {
    }

    void start() noexcept {
        unifex::start(innerOp_);
    }

    UNIFEX_NO_UNIQUE_ADDRESS unifex::inplace_stop_source stop_source_;
    UNIFEX_NO_UNIQUE_ADDRESS _stop_source_operation_callback<
            SuccessorFactory, Receiver, remove_cvref_t<decltype(unifex::get_stop_token(std::declval<Receiver>()))>>
        stop_callback_;
    connect_result_t<
        std::invoke_result_t<SuccessorFactory, unifex::inplace_stop_source&>,
        stop_source_receiver<operation<SuccessorFactory, Receiver>, remove_cvref_t<Receiver>>>
        innerOp_;
};

struct _fn {

    template<typename SuccessorFactory>
    auto operator()(SuccessorFactory&& factory) const
        noexcept(std::is_nothrow_constructible_v<remove_cvref_t<SuccessorFactory>, SuccessorFactory>)
        -> stop_source_sender<remove_cvref_t<SuccessorFactory>> {
        return stop_source_sender<remove_cvref_t<SuccessorFactory>>{(SuccessorFactory&&)factory};
    }
};

} // namespace _let_with_stop_source

inline constexpr _let_with_stop_source::_fn let_with_stop_source{};


} // namespace unifex

#include <unifex/detail/epilogue.hpp>
