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

#include <algorithm>

#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/get_execution_policy.hpp>
#include <unifex/execution_policy.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/bind_back.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
// Size of chunk used for cancellation allowing for some vectorisation
constexpr size_t bulk_cancellation_chunk_size = 16;

namespace _bulk_schedule {

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
        noexcept(is_nothrow_receiver_of_v<Receiver> &&
                 is_nothrow_next_receiver_v<Receiver, Integral>) {
        using policy_t = decltype(get_execution_policy(receiver_));
        auto stop_token = get_stop_token(receiver_);
        const bool stop_possible = !is_stop_never_possible_v<decltype(stop_token)> && stop_token.stop_possible();

        if(stop_possible) {
            for (Integral chunk_start(0); chunk_start < count_; chunk_start += bulk_cancellation_chunk_size) {
                if(stop_token.stop_requested()) {
                    unifex::set_done(std::move(receiver_));
                    return;
                }
                Integral chunk_end = std::min(chunk_start + static_cast<Integral>(bulk_cancellation_chunk_size), count_);
                if constexpr (is_one_of_v<policy_t, unsequenced_policy, parallel_unsequenced_policy>) {
UNIFEX_DIAGNOSTIC_PUSH

                    // Vectorisable version
#if defined(__clang__)
                    // When optimizing for size (e.g. with -Oz), Clang will not
                    // vectorize this loop, and will emit a warning.  There's
                    // nothing to be done about the warning, though, so just
                    // suppress it.
                    #pragma clang diagnostic ignored "-Wpass-failed"
                    #pragma clang loop vectorize(enable) interleave(enable)
#elif defined(__GNUC__)
                    #pragma GCC ivdep
#elif defined(_MSC_VER)
                    #pragma loop(ivdep)
#endif
                    for (Integral i(chunk_start); i < chunk_end; ++i) {
                        unifex::set_next(receiver_, Integral(i));
                    }

UNIFEX_DIAGNOSTIC_POP
                } else {
                    // Sequenced version
                    for (Integral i(chunk_start); i < chunk_end; ++i) {
                        unifex::set_next(receiver_, Integral(i));
                    }
                }
            }
        } else {
            if constexpr (is_one_of_v<policy_t, unsequenced_policy, parallel_unsequenced_policy>) {
UNIFEX_DIAGNOSTIC_PUSH

                // Vectorisable version
#if defined(__clang__)
                // When optimizing for size (e.g. with -Oz), Clang will not
                // vectorize this loop, and will emit a warning.  There's
                // nothing to be done about the warning, though, so just
                // suppress it.
                #pragma clang diagnostic ignored "-Wpass-failed"
                #pragma clang loop vectorize(enable) interleave(enable)
#elif defined(__GNUC__)
                #pragma GCC ivdep
#elif defined(_MSC_VER)
                #pragma loop(ivdep)
#endif
                for (Integral i(0); i < count_; ++i) {
                    unifex::set_next(receiver_, Integral(i));
                }

UNIFEX_DIAGNOSTIC_POP
            } else {
                // Sequenced version
                for (Integral i(0); i < count_; ++i) {
                    unifex::set_next(receiver_, Integral(i));
                }
            }
        }

        unifex::set_value(std::move(receiver_));
    }

    template(typename Error)
        (requires receiver<Receiver, Error>)
    void set_error(Error&& e) noexcept {
        unifex::set_error(std::move(receiver_), (Error&&)e);
    }

    void set_done() noexcept {
        unifex::set_done(std::move(receiver_));
    }

private:
    Integral count_;
    Receiver receiver_;
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
    using error_types = sender_error_types_t<schedule_sender_t, Variant>;

    static constexpr bool sends_done = true;

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
    template <typename Integral>
    constexpr auto operator()(Integral n) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, Integral>)
        -> bind_back_result_t<_fn, Integral> {
      return bind_back(*this, n);
    }
};

} // namespace _bulk_schedule

inline constexpr _bulk_schedule::_fn bulk_schedule{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
