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

#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/manual_lifetime.hpp>

#include <windows.h>
#include <threadpoolapiset.h>

#include <utility>
#include <exception>
#include <new>
#include <system_error>
#include <atomic>
#include <cassert>
#include <cstdio>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace win32 {

class windows_thread_pool {
    class scheduler;
    class schedule_sender;
    class schedule_op_base;
    template<typename Receiver>
    struct _schedule_op {
        class type;
    };
    template<typename Receiver>
    using schedule_op = typename _schedule_op<Receiver>::type;


    template<typename StopToken>
    struct _cancellable_schedule_op_base {
        class type;
    };
    template<typename StopToken>
    using cancellable_schedule_op_base = typename _cancellable_schedule_op_base<StopToken>::type;

    template<typename Receiver>
    struct _cancellable_schedule_op {
        class type;
    };
    template <typename Receiver>
    using cancellable_schedule_op = typename _cancellable_schedule_op<Receiver>::type;

public:

    // Initialise to use the process' default thread-pool.
    windows_thread_pool() noexcept;

    // Construct to an independend thread-pool with a dynamic number of
    // threads that varies between a min and a max number of threads.
    explicit windows_thread_pool(std::uint32_t minThreadCount, std::uint32_t maxThreadCount);

    ~windows_thread_pool();

    scheduler get_scheduler() noexcept;

private:
    PTP_POOL threadPool_;
};

/////////////////////////
// Non-cancellable schedule() operation

class windows_thread_pool::schedule_op_base {
public:
    schedule_op_base(schedule_op_base&&) = delete;
    schedule_op_base& operator=(schedule_op_base&&) = delete;

    ~schedule_op_base();

    void start() & noexcept;

protected:
    schedule_op_base(windows_thread_pool& pool, PTP_WORK_CALLBACK workCallback);

private:
    TP_CALLBACK_ENVIRON environ_;
    PTP_WORK work_;
};

template<typename Receiver>
class windows_thread_pool::_schedule_op<Receiver>::type final : public windows_thread_pool::schedule_op_base {
public:
    template<typename Receiver2>
    explicit type(windows_thread_pool& pool, Receiver2&& r)
    : schedule_op_base(pool, &work_callback)
    , receiver_((Receiver2&&)r)
    {}

private:
    static void CALLBACK work_callback(PTP_CALLBACK_INSTANCE instance, void* workContext, PTP_WORK work) noexcept {
        auto& op = *static_cast<type*>(workContext);
        if constexpr (is_nothrow_callable_v<decltype(unifex::set_value), Receiver>) {
            unifex::set_value(std::move(op.receiver_));
        } else {
            try {
                unifex::set_value(std::move(op.receiver_));
            } catch (...) {
                unifex::set_error(std::move(op.receiver_), std::current_exception());
            }
        }
    }

    Receiver receiver_;
};

///////////////////////////
// Cancellable schedule() operation

template<typename StopToken>
class windows_thread_pool::_cancellable_schedule_op_base<StopToken>::type {
public:
    type(type&&) = delete;
    type& operator=(type&&) = delete;

    ~type() {
        ::CloseThreadpoolWork(work_);
        ::DestroyThreadpoolEnvironment(&environ_);
        delete state_;
    }

protected:
    explicit type(windows_thread_pool& pool, bool isStopPossible) {
        ::InitializeThreadpoolEnvironment(&environ_);
        ::SetThreadpoolCallbackPool(&environ_, pool.threadPool_);

        work_ = ::CreateThreadpoolWork(
            isStopPossible ? &stoppable_work_callback : &unstoppable_work_callback,
            static_cast<void*>(this),
            &environ_);
        if (work_ == nullptr) {
            DWORD errorCode = ::GetLastError();
            ::DestroyThreadpoolEnvironment(&environ_);
            throw std::system_error{static_cast<int>(errorCode), std::system_category(), "CreateThreadpoolWork()"};
        }

        if (isStopPossible) {
            state_ = new (std::nothrow) std::atomic<std::uint32_t>(not_started);
            if (state_ == nullptr) {
                ::CloseThreadpoolWork(work_);
                ::DestroyThreadpoolEnvironment(&environ_);
                throw std::bad_alloc{};
            }
        } else {
            state_ = nullptr;
        }
    }

    void start_impl(const StopToken& stopToken) & noexcept {
        if (state_ != nullptr) {
            // Short-circuit all of this if stopToken.stop_requested() is already true.
            if (stopToken.stop_requested()) {
                set_done_impl();
                return;
            }

            stopCallback_.construct(stopToken, stop_requested_callback{*this});

            // Take a copy of the 'state' pointer prior to submitting the
            // work as the operation-state may have already been destroyed
            // on another thread by the time SubmitThreadpoolWork() returns.
            auto* state = state_;

            ::SubmitThreadpoolWork(work_);

            // Signal that SubmitThreadpoolWork() has returned and that it is
            // now safe for the stop-request to request cancellation of the
            // work items.
            const auto prevState = state->fetch_add(submit_complete_flag, std::memory_order_acq_rel);
            if ((prevState & stop_requested_flag) != 0) {
                // stop was requested before the call to SubmitThreadpoolWork()
                // returned and before the work started executing. It was not
                // safe for the request_stop() method to cancel the work before
                // it had finished being submitted so it has delegated responsibility
                // for cancelling the just-submitted work to us to do once we
                // finished submitting the work.
                complete_with_done();
            } else if ((prevState & running_flag) != 0) {
                // Otherwise, it's possible that the work item may have started
                // running on another thread already, prior to us returning.
                // If this is the case then, to avoid leaving us with a
                // dangling reference to the 'state' when the operation-state
                // is destroyed, it will detach the 'state' from the operation-state
                // and delegate the delete of the 'state' to us.
                delete state;
            }
        } else {
            // A stop-request is not possible so skip the extra
            // synchronisation needed to support it.
            ::SubmitThreadpoolWork(work_);
        }
    }

private:
    static void CALLBACK unstoppable_work_callback(
        PTP_CALLBACK_INSTANCE instance, void* workContext, PTP_WORK work) noexcept {
        auto& op = *static_cast<type*>(workContext);
        op.set_value_impl();
    }

    static void CALLBACK stoppable_work_callback(
            PTP_CALLBACK_INSTANCE instance, void* workContext, PTP_WORK work) noexcept {
        auto& op = *static_cast<type*>(workContext);

        // Signal that the work callback has started executing.
        auto prevState = op.state_->fetch_add(starting_flag, std::memory_order_acq_rel);
        if ((prevState & stop_requested_flag) != 0) {
            // request_stop() is already running and is waiting for this callback
            // to finish executing. So we return immediately here without doing
            // anything further so that we don't introduce a deadlock.
            // In particular, we don't want to try to deregister the stop-callback
            // which will block waiting for the request_stop() method to return.
            return;
        }

        // Note that it's possible that stop might be requested after setting
        // the 'starting' flag but before we deregister the stop callback.
        // We're going to ignore these stop-requests as we already won the race
        // in the fetch_add() above and ignoring them simplifies some of the
        // cancellation logic.

        op.stopCallback_.destruct();

        prevState = op.state_->fetch_add(running_flag, std::memory_order_acq_rel);
        if (prevState == starting_flag) {
            // start() method has not yet finished submitting the work
            // on another thread and so is still accessing the 'state'.
            // This means we don't want to let the operation-state destructor
            // free the state memory. Instead, we have just delegated
            // responsibility for freeing this memory to the start() method
            // and we clear the start_ member here to prevent the destructor
            // from freeing it.
            op.state_ = nullptr;
        }

        op.set_value_impl();
    }

    void request_stop() noexcept {
        auto prevState = state_->load(std::memory_order_relaxed);
        do {
            assert((prevState & running_flag) == 0);
            if ((prevState & starting_flag) != 0) {
                // Work callback won the race and will be waiting for
                // us to return so it can deregister the stop-callback.
                // Return immediately so we don't deadlock.
                return;
            }
        } while (!state_->compare_exchange_weak(
            prevState,
            prevState | stop_requested_flag,
            std::memory_order_acq_rel,
            std::memory_order_relaxed));

        assert((prevState & starting_flag) == 0);

        if ((prevState & submit_complete_flag) != 0) {
            // start() has finished calling SubmitThreadpoolWork() and the work has not
            // yet started executing the work so it's safe for this method to now try
            // and cancel the work. While it's possible that the work callback will start
            // executing concurrently on a thread-pool thread, we are guaranteed that
            // it will see our write of the stop_requested_flag and will promptly
            // return without blocking.
            complete_with_done();
        } else {
            // Otherwise, as the start() method has not yet finished calling
            // SubmitThreadpoolWork() we can't safely call WaitForThreadpoolWorkCallbacks().
            // In this case we are delegating responsibility for calling complete_with_done()
            // to start() method when it eventually returns from SubmitThreadpoolWork().
        }
    }

    void complete_with_done() noexcept {
        const BOOL cancelPending = TRUE;
        ::WaitForThreadpoolWorkCallbacks(work_, cancelPending);

        // Destruct the stop-callback before calling set_done() as the call
        // to set_done() will invalidate the stop-token and we need to 
        // make sure that 
        stopCallback_.destruct();

        // Now that the work has been successfully cancelled we can
        // call the receiver's set_done().
        set_done_impl();
    }

    virtual void set_done_impl() noexcept = 0;
    virtual void set_value_impl() noexcept = 0;

    struct stop_requested_callback {
        type& op_;

        void operator()() noexcept {
            op_.request_stop();
        }
    };

    /////////////////
    // Flags to use for state_ member

    // Initial state. start() not yet called.
    static constexpr std::uint32_t not_started = 0;

    // Flag set once start() has finished calling ThreadpoolSubmitWork()
    static constexpr std::uint32_t submit_complete_flag = 1;

    // Flag set by request_stop() 
    static constexpr std::uint32_t stop_requested_flag = 2;

    // Flag set by cancellable_work_callback() when it starts executing.
    // This is before deregistering the stop-callback.
    static constexpr std::uint32_t starting_flag = 4;

    // Flag set by cancellable_work_callback() after having deregistered
    // the stop-callback, just before it calls the receiver.
    static constexpr std::uint32_t running_flag = 8; 

    PTP_WORK work_;
    TP_CALLBACK_ENVIRON environ_;
    std::atomic<std::uint32_t>* state_;
    manual_lifetime<typename StopToken::template callback_type<stop_requested_callback>> stopCallback_;
};

template<typename Receiver>
class windows_thread_pool::_cancellable_schedule_op<Receiver>::type final
    : public windows_thread_pool::cancellable_schedule_op_base<stop_token_type_t<Receiver>> {
    using base = windows_thread_pool::cancellable_schedule_op_base<stop_token_type_t<Receiver>>;
public:
    template<typename Receiver2>
    explicit type(windows_thread_pool& pool, Receiver2&& r)
    : base(pool, unifex::get_stop_token(r).stop_possible())
    , receiver_((Receiver2&&)r)
    {}

    void start() & noexcept {
        this->start_impl(get_stop_token(receiver_));
    }

private:
    void set_value_impl() noexcept override {
        if constexpr (is_nothrow_callable_v<decltype(unifex::set_value), Receiver>) {
            unifex::set_value(std::move(receiver_));
        } else {
            try {
                unifex::set_value(std::move(receiver_));
            } catch (...) {
                unifex::set_error(std::move(receiver_), std::current_exception());
            }
        }
    }

    void set_done_impl() noexcept override {
        unifex::set_done(std::move(receiver_));
    }

    Receiver receiver_;
};

class windows_thread_pool::schedule_sender {
public:
    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template<template<typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template(typename Receiver)
        (requires receiver_of<Receiver> AND is_stop_never_possible_v<Receiver>)
    schedule_op<unifex::remove_cvref_t<Receiver>> connect(Receiver&& r) const {
        return schedule_op<unifex::remove_cvref_t<Receiver>>{
            *pool_, (Receiver&&)r};
    }

    template(typename Receiver)
        (requires receiver_of<Receiver> AND
            (!is_stop_never_possible_v<stop_token_type_t<Receiver>>))
    cancellable_schedule_op<unifex::remove_cvref_t<Receiver>> connect(Receiver&& r) const {
        return cancellable_schedule_op<unifex::remove_cvref_t<Receiver>>{
            *pool_, (Receiver&&)r};
    }

private:
    friend scheduler;

    explicit schedule_sender(windows_thread_pool& pool) noexcept
    : pool_(&pool)
    {}

    windows_thread_pool* pool_;
};

class windows_thread_pool::scheduler {
public:
    schedule_sender schedule() const noexcept {
        return schedule_sender{*pool_};
    }

    friend bool operator==(scheduler a, scheduler b) noexcept {
        return a.pool_ == b.pool_;
    }

private:
    friend windows_thread_pool;

    explicit scheduler(windows_thread_pool& pool) noexcept
    : pool_(&pool)
    {}

    windows_thread_pool* pool_;
};

inline windows_thread_pool::scheduler windows_thread_pool::get_scheduler() noexcept {
    return scheduler{*this};
}

} // namespace win32
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
