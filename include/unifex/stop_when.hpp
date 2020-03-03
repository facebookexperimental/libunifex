/*
 * Copyright 2020-present Facebook, Inc.
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

#include <unifex/sender_concepts.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/type_list.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/tag_invoke.hpp>

#include <type_traits>
#include <tuple>
#include <variant>
#include <optional>
#include <atomic>
#include <functional>

namespace unifex
{

namespace detail
{
    template<typename Source, typename Trigger, typename Receiver>
    class stop_when_operation;

    template<typename Source, typename Trigger, typename Receiver>
    class stop_when_source_receiver {
        using operation_state = stop_when_operation<Source, Trigger, Receiver>;
    public:
        explicit stop_when_source_receiver(operation_state* op) noexcept
        : op_(op) {}

        stop_when_source_receiver(stop_when_source_receiver&& other) noexcept
        : op_(std::exchange(other.op_, nullptr))
        {}

        template<typename... Values>
        void set_value(Values&&... values) && {
            op_->result_.template emplace<std::tuple<tag_t<unifex::set_value>, std::decay_t<Values>...>>(
                unifex::set_value, (Values&&)values...);
            op_->notify_source_complete();
        }

        template<typename Error>
        void set_error(Error&& error) && noexcept {
            op_->result_.template emplace<std::tuple<tag_t<unifex::set_error>, std::decay_t<Error>>>(
                unifex::set_error, (Error&&)error);
            op_->notify_source_complete();
        }

        void set_done() && noexcept {
            op_->result_.template emplace<std::tuple<tag_t<unifex::set_done>>>(unifex::set_done);
            op_->notify_source_complete();
        }

    private:
        friend inplace_stop_token tag_invoke(tag_t<unifex::get_stop_token>, const stop_when_source_receiver& r) noexcept {
            return r.get_stop_token();
        }

        template<
            typename CPO, typename... Args,
            std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
        friend auto tag_invoke(CPO cpo, const stop_when_source_receiver& r, Args&&... args)
            noexcept(std::is_nothrow_invocable_v<CPO, const Receiver&, Args...>)
            -> std::invoke_result_t<CPO, const Receiver&, Args...> {
            return std::invoke(std::move(cpo), r.get_receiver(), (Args&&)args...);
        }

        inplace_stop_token get_stop_token() const noexcept {
            return op_->stopSource_.get_token();
        }

        const Receiver& get_receiver() const noexcept {
            return op_->receiver_;
        }

        operation_state* op_;
    };

    template<typename Source, typename Trigger, typename Receiver>
    class stop_when_trigger_receiver {
        using operation_state = stop_when_operation<Source, Trigger, Receiver>;
    public:
        explicit stop_when_trigger_receiver(operation_state* op) noexcept
        : op_(op) {}

        stop_when_trigger_receiver(stop_when_trigger_receiver&& other) noexcept
        : op_(std::exchange(other.op_, nullptr))
        {}

        void set_value() && noexcept {
            op_->notify_trigger_complete();
        }

        template<typename Error>
        void set_error(Error&&) && noexcept {
            op_->notify_trigger_complete();
        }

        void set_done() && noexcept {
            op_->notify_trigger_complete();
        }

    private:

        friend inplace_stop_token tag_invoke(tag_t<unifex::get_stop_token>, const stop_when_trigger_receiver& r) noexcept {
            return r.get_stop_token();
        }

        template<
            typename CPO, typename... Args,
            std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
        friend auto tag_invoke(CPO cpo, const stop_when_trigger_receiver& r, Args&&... args)
            noexcept(std::is_nothrow_invocable_v<CPO, const Receiver&, Args...>)
            -> std::invoke_result_t<CPO, const Receiver&, Args...> {
            return std::invoke(std::move(cpo), r.get_receiver(), (Args&&)args...);
        }

        inplace_stop_token get_stop_token() const noexcept {
            return op_->stopSource_.get_token();
        }

        const Receiver& get_receiver() const noexcept {
            return op_->receiver_;
        }

        operation_state* op_;
    };

    template<typename Source, typename Trigger, typename Receiver>
    class stop_when_operation {
        using source_receiver = stop_when_source_receiver<Source, Trigger, Receiver>;
        using trigger_receiver = stop_when_trigger_receiver<Source, Trigger, Receiver>;
    public:
        template<typename Receiver2>
        explicit stop_when_operation(Source&& source, Trigger&& trigger, Receiver2&& receiver)
            noexcept(is_nothrow_connectable_v<Source, source_receiver> &&
                     is_nothrow_connectable_v<Trigger, trigger_receiver> &&
                     std::is_nothrow_constructible_v<Receiver, Receiver2>)
            : receiver_((Receiver2&&)receiver)
            , sourceOp_(unifex::connect((Source&&)source, source_receiver{this}))
            , triggerOp_(unifex::connect((Trigger&&)trigger, trigger_receiver{this}))
        {}
    
        void start() & noexcept {
            stopCallback_.emplace(
                get_stop_token(receiver_),
                cancel_callback{this});

            unifex::start(sourceOp_);
            unifex::start(triggerOp_);
        }

    private:
        friend class stop_when_source_receiver<Source, Trigger, Receiver>;
        friend class stop_when_trigger_receiver<Source, Trigger, Receiver>;

        class cancel_callback {
        public:
            explicit cancel_callback(stop_when_operation* op) noexcept
            : op_(op) {}

            void operator()() noexcept {
                op_->stopSource_.request_stop();
            }

        private:
            stop_when_operation* op_;
        };

        void notify_source_complete() noexcept {
            this->notify_trigger_complete();
        }

        void notify_trigger_complete() noexcept {
            stopSource_.request_stop();
            if (activeOpCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                stopCallback_.reset();
                deliver_result();
            }
        }

        void deliver_result() noexcept {
            try {
                std::visit([this](auto&& tuple) {
                    if constexpr (std::tuple_size_v<std::remove_reference_t<decltype(tuple)>> != 0) {
                        std::apply([&](auto set_xxx, auto&&... args) {
                            set_xxx(std::move(receiver_), static_cast<decltype(args)>(args)...);
                        }, static_cast<decltype(tuple)>(tuple));
                    } else {
                        // Should be unreachable
                        std::terminate();
                    }
                }, std::move(result_));
            } catch (...) {
                unifex::set_error(std::move(receiver_), std::current_exception());
            }
        }

        template<typename... Values>
        using value_decayed_tuple = std::tuple<tag_t<unifex::set_value>, std::decay_t<Values>...>;

        template<typename... Errors>
        using error_tuples = type_list<std::tuple<tag_t<unifex::set_error>, std::decay_t<Errors>>...>;

        using result_variant = typename concat_type_lists_t<
            type_list<std::tuple<>, std::tuple<tag_t<unifex::set_done>>>,
            typename std::remove_reference_t<Source>::template value_types<type_list, value_decayed_tuple>,
            typename std::remove_reference_t<Source>::template error_types<error_tuples>>::template apply<std::variant>;

        UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
        std::atomic<int> activeOpCount_ = 2;
        inplace_stop_source stopSource_;
        std::optional<
            typename stop_token_type_t<Receiver>::template callback_type<cancel_callback>>
            stopCallback_;
        UNIFEX_NO_UNIQUE_ADDRESS result_variant result_;
        UNIFEX_NO_UNIQUE_ADDRESS operation_t<Source, source_receiver> sourceOp_;
        UNIFEX_NO_UNIQUE_ADDRESS operation_t<Trigger, trigger_receiver> triggerOp_;
    };

    template<typename Source, typename Trigger>
    class stop_when_sender {
        template<typename... Values>
        using decayed_type_list = type_list<type_list<std::decay_t<Values>...>>;

        template<template<typename...> class Outer, template<typename...> class Inner>
        struct compose_nested {
            template<typename... Lists>
            using apply = Outer<typename Lists::template apply<Inner>...>;
        };

    public:
        template<
            template<typename...> class Variant,
            template<typename...> class Tuple>
        using value_types = 
            typename Source::template value_types<
                concat_type_lists_unique_t, decayed_type_list>::template apply<
                    compose_nested<Variant, Tuple>::template apply>;

        template<
            template<typename...> class Variant>
        using error_types = typename concat_type_lists_unique_t<
            typename Source::template error_types<decayed_tuple<type_list>::template apply>,
            type_list<std::exception_ptr>>::template apply<Variant>;

        template<typename Source2, typename Trigger2>
        explicit stop_when_sender(Source2&& source, Trigger2&& trigger)
            noexcept(std::is_nothrow_constructible_v<Source, Source2> && std::is_nothrow_constructible_v<Trigger, Trigger2>)
            : source_((Source2&&)source)
            , trigger_((Trigger2&&)trigger)
            {}

        template<
            typename Receiver,
            std::enable_if_t<
                is_connectable_v<Source, stop_when_source_receiver<Source, Trigger, std::remove_cvref_t<Receiver>>> &&
                is_connectable_v<Trigger, stop_when_trigger_receiver<Source, Trigger, std::remove_cvref_t<Receiver>>>,
                int> = 0>
        auto connect(Receiver&& r) && -> stop_when_operation<Source, Trigger, std::remove_cvref_t<Receiver>> {
            return stop_when_operation<Source, Trigger, std::remove_cvref_t<Receiver>>{
                (Source&&)source_,
                (Trigger&&)trigger_,
                (Receiver&&)r
            };
        }

        template<
            typename Receiver,
            std::enable_if_t<
                is_connectable_v<Source&, stop_when_source_receiver<Source&, Trigger&, std::remove_cvref_t<Receiver>>> &&
                is_connectable_v<Trigger&, stop_when_trigger_receiver<Source&, Trigger&, std::remove_cvref_t<Receiver>>>,
                int> = 0>
        auto connect(Receiver&& r) & -> stop_when_operation<Source&, Trigger&, std::remove_cvref_t<Receiver>> {
            return stop_when_operation<Source&, Trigger&, std::remove_cvref_t<Receiver>>{
                source_,
                trigger_,
                (Receiver&&)r
            };
        }

        template<
            typename Receiver,
            std::enable_if_t<
                is_connectable_v<Source&, stop_when_source_receiver<const Source&, const Trigger&, std::remove_cvref_t<Receiver>>> &&
                is_connectable_v<Trigger&, stop_when_trigger_receiver<const Source&, const Trigger&, std::remove_cvref_t<Receiver>>>,
                int> = 0>
        auto connect(Receiver&& r) const & -> stop_when_operation<const Source&, const Trigger&, std::remove_cvref_t<Receiver>> {
            return stop_when_operation<const Source&, const Trigger&, std::remove_cvref_t<Receiver>>{
                std::as_const(source_),
                std::as_const(trigger_),
                (Receiver&&)r
            };
        }

    private:
        UNIFEX_NO_UNIQUE_ADDRESS Source source_;
        UNIFEX_NO_UNIQUE_ADDRESS Trigger trigger_;
    };
} // namespace detail

inline constexpr struct stop_when_cpo {
    template<typename Source, typename Trigger>
    auto operator()(Source&& source, Trigger& trigger) const
        noexcept(is_nothrow_tag_invocable_v<stop_when_cpo, Source, Trigger>)
        -> tag_invoke_result_t<stop_when_cpo, Source, Trigger>{
        return unifex::tag_invoke(*this, (Source&&)source, (Trigger&&)trigger);    
    }

    template<
        typename Source,
        typename Trigger,
        std::enable_if_t<!is_tag_invocable_v<stop_when_cpo, Source, Trigger>, int> = 0>
    auto operator()(Source&& source, Trigger&& trigger) const
        noexcept(std::is_nothrow_constructible_v<
            detail::stop_when_sender<std::remove_cvref_t<Source>, std::remove_cvref_t<Trigger>>, Source, Trigger>)
        -> detail::stop_when_sender<std::remove_cvref_t<Source>, std::remove_cvref_t<Trigger>> {
        return detail::stop_when_sender<std::remove_cvref_t<Source>, std::remove_cvref_t<Trigger>>(
            (Source&&)source, (Trigger&&)trigger);
    }
} stop_when;

} // namespace unifex
