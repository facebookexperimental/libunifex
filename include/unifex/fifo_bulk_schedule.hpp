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

#include <algorithm>

#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/get_execution_policy.hpp>
#include <unifex/execution_policy.hpp>
#include <unifex/get_stop_token.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _fifo_bulk_schedule {

template<typename Integral, typename Receiver>
struct _schedule_receiver {
    class type;
};

template<typename Integral, typename Receiver>
using schedule_receiver = typename _schedule_receiver<Integral, Receiver>::type;

template<typename Integral, typename Receiver>
class _schedule_receiver<Integral, Receiver>::type {
public:
    template<typename Receiver2>
    explicit type(Integral count, Receiver2&& r)
    : count_(std::move(count))
    , receiver_((Receiver2&&)r)
    {}

    void set_value()
        noexcept(is_nothrow_value_receiver_v<Receiver> &&
                 is_nothrow_next_receiver_v<Receiver, Integral>) {
        std::cout << "bulk_schedule set_value\n";
        using policy_t = decltype(get_execution_policy(receiver_));
        auto stop_token = get_stop_token(receiver_);
        const bool stop_possible = !is_stop_never_possible_v<decltype(stop_token)> && stop_token.stop_possible();

        // Sequenced version
        for (Integral i(0); i < count_; ++i) {
            unifex::set_next(receiver_, Integral(i));
        }

        // FIFO_CHANGES TODO: Drop this set_value call on eager start
        // which probably means moving the eagerness in here from
        // the manual event loop, which is easier once
        // this merges in.
        unifex::set_value(std::move(receiver_));
    }

    template(typename Error)
        (requires is_error_receiver_v<Receiver, Error>)
    void set_error(Error&& e) noexcept {
        unifex::set_error(std::move(receiver_), (Error&&)e);
    }

    template(typename R = Receiver)
        (requires is_done_receiver_v<Receiver>)
    void set_done() noexcept {
        unifex::set_done(std::move(receiver_));
    }

    // FIFO_CHANGES: This is a fifo context if its successor is
    friend void* tag_invoke(tag_t<get_fifo_context>, type& rec) noexcept {
        return get_fifo_context(rec.receiver_);
    }

    // FIFO_CHANGES: Forward through start eagerly requests
    friend bool tag_invoke(tag_t<start_eagerly>, type& rec) noexcept {
        return start_eagerly(rec.receiver_);
    }

private:
    Receiver receiver_;
    Integral count_;
};

template<typename Scheduler, typename Integral>
struct _default_sender {
    class type;
};

template<typename Scheduler, typename Integral>
using default_sender = typename _default_sender<Scheduler, Integral>::type;

template<typename Scheduler, typename Integral>
class _default_sender<Scheduler, Integral>::type {
    using schedule_sender_t =
        decltype(unifex::schedule(UNIFEX_DECLVAL(const Scheduler&)));

public:
    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template<template<typename...> class Variant, template<typename...> class Tuple>
    using next_types = Variant<Tuple<Integral>>;

    template<template<typename...> class Variant>
    using error_types = typename schedule_sender_t::template error_types<Variant>;

    static constexpr bool sends_done = schedule_sender_t::sends_done;

    template<typename Scheduler2>
    explicit type(Scheduler2&& s, Integral count)
    : scheduler_(static_cast<Scheduler2&&>(s))
    , count_(std::move(count))
    {}

    template(typename Self, typename BulkReceiver)
        (requires
            same_as<remove_cvref_t<Self>, type> AND
            receiver_of<BulkReceiver> AND
            is_next_receiver_v<BulkReceiver, Integral>)
    friend auto tag_invoke(tag_t<unifex::connect>, Self&& s, BulkReceiver&& r) {
        return unifex::connect(
            unifex::schedule(static_cast<Self&&>(s).scheduler_),
            schedule_receiver<Integral, remove_cvref_t<BulkReceiver>>{
                static_cast<Self&&>(s).count_,
                static_cast<BulkReceiver&&>(r)});
    }

    // FIFO_CHANGES: This is a fifo context, return its loop_ as an id
    friend void* tag_invoke(tag_t<get_fifo_context>, type& send) noexcept {
        return get_fifo_context(send.scheduler_);
    }

private:
    Scheduler scheduler_;
    Integral count_;
};

struct _fn {
    template(typename Scheduler, typename Integral)
        (requires
            tag_invocable<_fn, Scheduler, Integral>)
    auto operator()(Scheduler&& s, Integral n) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Scheduler, Integral>)
        -> tag_invoke_result_t<_fn, Scheduler, Integral> {
        return tag_invoke(_fn{}, (Scheduler&&)s, std::move(n));
    }

    template(typename Scheduler, typename Integral)
        (requires
            scheduler<Scheduler> AND
            (!tag_invocable<_fn, Scheduler, Integral>))
    auto operator()(Scheduler&& s, Integral n) const
        noexcept(
            std::is_nothrow_constructible_v<remove_cvref_t<Scheduler>, Scheduler> &&
            std::is_nothrow_move_constructible_v<Integral>)
        -> default_sender<remove_cvref_t<Scheduler>, Integral> {
        return default_sender<remove_cvref_t<Scheduler>, Integral>{(Scheduler&&)s, std::move(n)};
    }
};

} // namespace _fifo_bulk_schedule

inline constexpr _fifo_bulk_schedule::_fn fifo_bulk_schedule{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
