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

// #define TP_LOG(X) std::puts(X)
#define TP_LOG(X)

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace win32 {

class windows_thread_pool {
    class scheduler;
    class schedule_sender;
    
    template<typename Receiver>
    struct _schedule_op {
        class type;
    };
    template<typename Receiver>
    using schedule_op = typename _schedule_op<Receiver>::type;

    template<typename Receiver>
    struct _cancellable_schedule_op {
        class type;
    };
    template <typename Receiver>
    using cancellable_schedule_op = typename _cancellable_schedule_op<Receiver>::type;

public:
    windows_thread_pool() {
        threadPool_ = nullptr; // use default thread-pool
    }

    ~windows_thread_pool() {
        if (threadPool_ != nullptr) {
            ::CloseThreadpool(threadPool_);
        }
    }

    scheduler get_scheduler() noexcept;

private:
    PTP_POOL threadPool_;
};

template<typename Receiver>
class windows_thread_pool::_schedule_op<Receiver>::type {
public:
    template<typename Receiver2>
    explicit type(windows_thread_pool& pool, Receiver2&& r)
    : receiver_((Receiver2&&)r) {
        ::InitializeThreadpoolEnvironment(&environ_);
        ::SetThreadpoolCallbackPool(&environ_, pool.threadPool_);
        work_ = ::CreateThreadpoolWork(&work_callback, this, &environ_);
        if (work_ == nullptr) {
            // TODO: Should we just cache the error and deliver via set_error(receiver_, std::error_code{})
            // upon start()?
            DWORD errorCode = ::GetLastError();
            ::DestroyThreadpoolEnvironment(&environ_);
            throw std::system_error{static_cast<int>(errorCode), std::system_category(), "CreateThreadpoolWork()"};
        }
    }

    ~type() {
        ::CloseThreadpoolWork(work_);
        ::DestroyThreadpoolEnvironment(&environ_);
    }

    void start() & noexcept {
        ::SubmitThreadpoolWork(work_);
    }

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
    TP_CALLBACK_ENVIRON environ_;
    PTP_WORK work_;
};

template<typename Receiver>
class windows_thread_pool::_cancellable_schedule_op<Receiver>::type {
public:
    template<typename Receiver2>
    explicit type(windows_thread_pool& pool, Receiver2&& r)
    : receiver_((Receiver2&&)r) {
        ::TpInitializeCallbackEnviron(&environ_);
        ::SetThreadpoolCallbackPool(&environ_, pool.threadPool_);        

        PTP_WORK_CALLBACK callback = &work_callback;
        if (get_stop_token(receiver_).stop_possible()) {
            TP_LOG("creating cleanup group");
            cleanupGroup_ = ::CreateThreadpoolCleanupGroup();
            if (cleanupGroup_ == NULL) {
                DWORD errorCode = ::GetLastError();
                ::DestroyThreadpoolEnvironment(&environ_);
                TP_LOG("error creating cleanup group");
                throw std::system_error{static_cast<int>(errorCode), std::system_category(), "CreateThreadpoolCleanupGroup"};
            }
        
            ::SetThreadpoolCallbackCleanupGroup(&environ_, cleanupGroup_, &cancelled_callback);

            state_ = new (std::nothrow) std::atomic<std::uint32_t>(not_started);
            if (state_ == nullptr) {
                ::CloseThreadpoolCleanupGroup(cleanupGroup_);
                ::DestroyThreadpoolEnvironment(&environ_);
                TP_LOG("error allocating state");
                throw std::bad_alloc{};
            }

            callback = &cancellable_work_callback;
        }

        work_ = ::CreateThreadpoolWork(callback, this, &environ_);
        if (work_ == NULL) {
            DWORD errorCode = ::GetLastError();
            if (cleanupGroup_ != nullptr) {
                ::CloseThreadpoolCleanupGroup(cleanupGroup_);
            }            
            delete state_;
            ::DestroyThreadpoolEnvironment(&environ_);
            TP_LOG("error threadpool work");
            throw std::system_error{static_cast<int>(errorCode), std::system_category(), "CreateThreadpoolWork"};
        }
    }

    ~type() {
        TP_LOG("in op state destructor");
        if (work_ != nullptr) {
            ::CloseThreadpoolWork(work_);
        }
        if (cleanupGroup_ != nullptr) {
            ::CloseThreadpoolCleanupGroup(cleanupGroup_);
        }
        ::DestroyThreadpoolEnvironment(&environ_);
        delete state_;
    }

    void start() & noexcept {
        if (cleanupGroup_ != NULL) {
            TP_LOG("starting cancellable");
            start_cancellable();
        } else {
            TP_LOG("starting non-cancellable");
            // Cancellation not possible
            // Don't worry about the extra synchronisation needed to
            // support cancellation of the work.
            ::SubmitThreadpoolWork(work_);
        }
    }

private:
    void start_cancellable() noexcept {
        TP_LOG("register stop callback");
            cancelCallback_.construct(get_stop_token(receiver_), request_cancel_callback{*this});

            // Take a copy of the pointer to heap-allocated state prior
            // to submitting the work as the operation-state may have
            // already been destroyed on another thread by the time
            // SubmitThreadpoolWork() returns.
            auto* state = state_;

        TP_LOG("submitting work");
            ::SubmitThreadpoolWork(work_);
        TP_LOG("submitted work");

            // Now that we've finished calling SubmitThreadpoolWork() we can
            // signal in the shared atomic state_ that the task is now started.
            // Note that the cancellable_work_callback may have already started
            // executing on a thread-pool thread.
            auto oldState = state->fetch_add(submit_complete_flag, std::memory_order_acq_rel);
            if ((oldState & (cancel_requested_flag | running_flag)) == cancel_requested_flag) {
                // Cancellation was requested before SubmitThreadpoolWork() returned
                // and so the cancellation callback has delegated responsibility for
                // cancelling the work to the start() method.
                //
                // This will cause the cleanup_callback() to be run which will
                // do the set_done() call once the cancellation of the PTP_WORK
                // is complete.
                TP_LOG("cancelling work from start()");

                const BOOL cancelPending = TRUE;
                ::CloseThreadpoolCleanupGroupMembers(cleanupGroup_, cancelPending, nullptr);
            } else {
                // Several other possibilities here.
                // (starting / starting + running) x cancel_requested

                // We don't need to worry about cancellation here, we only need
                // to make sure that the 'state' allocation is freed.
                // If the cancellable_work_callback has already set the 'running_flag'
                // then this means it's finished with the 'state' and has delegated
                // freeing the memory to us.
                if ((oldState & running_flag) != 0) {
                    TP_LOG("deleting state from start()");
                    delete state;
                }
            }
    }

    void request_cancel() noexcept {
        // Signal an intent to call CloseThreadpoolCleanupGroupMembers()
        auto oldState = state_->fetch_add(cancel_requested_flag, std::memory_order_acq_rel);

        TP_LOG("in stop callback");

        // work_callback should have unsubscribed this callback before setting the running_flag.
        assert((oldState & running_flag) == 0);

        if (oldState == submit_complete_flag) {
            // SubmitThreadpoolWork() has returned but the cancellable_work_callback has not
            // yet started running. It is safe, therefore, to close the callback group
            // members here. If this is racing with the cancellable_work_callback running
            // on another thread then it will see our write to 'state' and will return immediately.
            // Once it's returned (if it started) the cancelled_callback() will be run
            // and this will do the actual handling of the cancellation.
            TP_LOG("cancelling work from stop callback");
            const BOOL cancelPending = TRUE;
            ::CloseThreadpoolCleanupGroupMembers(cleanupGroup_, cancelPending, nullptr);
        } else {
            if ((oldState & starting_flag) == 0) {
                TP_LOG("stop delegating call to close cleanup group to start()");
            } else {
                TP_LOG("work callback is already running, can't cancel");
            }
            // Otherwise there are two posible cases where we don't need to do anything:
            // - not_started - the start() method is still executing and hasn't finished
            //                 calling SubmitThreadpoolWork() yet, so it's not safe to
            //                 call CloseThreadpoolCleanupGroupMembers(). In this case
            //                 the start() method will see our write to 'state' and will
            //                 close the cleanup group when it eventually returns from
            //                 SubmitThreadpoolWork().
            // - starting_flag / starting_flag + submit_complete_flag
            //                 The cancellable_work_callback started executing before
            //                 cancellation was requested.
            //                 In this case it will be attempting to deregister this stop_callback
            //                 and will be blocked waiting for this callback to return, so we
            //                 don't want to close the cleanup group here as that will wait
            //                 for the cancellable_work_callback to return which will deadlock.
            //                 So we'll just return here and let the work_callback call pick
            //                 up the fact that cancellation was requested after deregistering
            //                 this stop-callback. Note that currently it will ignore such a
            //                 cancellation request anyway, since it's a rare race and it was
            //                 already running on a thread-pool thread.
        }
    }

    static void CALLBACK cancellable_work_callback(PTP_CALLBACK_INSTANCE instance, void* workContext, PTP_WORK work) noexcept {
        auto& op = *static_cast<type*>(workContext);

        TP_LOG("in work callback, about to mark as starting");

        // Signal that the work_callback has started executing.
        auto oldState = op.state_->fetch_add(starting_flag, std::memory_order_acq_rel);
        if ((oldState & cancel_requested_flag) != 0) {
            // Some thread has requested cancellation and is calling or about to
            // call CloseThreadpoolCleanupGroupMembers() which is going to block
            // on the cancellable_work_callback function returning.
            
            // We'll return immediately to avoid deadlocking with the cancellation
            // request and let the CloseThreadpoolCleanupGroupMembers()
            // function call the cancelled_callback to do the remaining cleanup work.
            TP_LOG("work already cancelled");
            return;
        }

        TP_LOG("deregistering stop callback from work callback");

        // Otherwise, we now deregister the cancellation callback before then
        // signalling that we are calling the receiver. If cancellation is
        // requested concurrently on another thread then this will block waiting
        // for the callback to finish. This is safe from deadlock because we
        // successfully set the 'starting' flag on the state before the cancel
        // request set the 'cancel_requested' flag and so we're guaranteed that
        // it will see our write of the 'starting' flag and so will not attempt
        // to close the cleanup group (which would block on the work callback returning).
        op.cancelCallback_.destruct();

        TP_LOG("stop callback deregistered, about to mark as running");

        // Signal that we're about to call the receiver.
        oldState = op.state_->fetch_add(running_flag, std::memory_order_acq_rel);
        if ((oldState & submit_complete_flag) == 0) {
            // Not safe to delete the 'state' allocation as the start() method is
            // still referencing it. We have now delegated responsibility for
            // freeing this memory to start() and so we need to clear out the
            // op.state_ pointer here so the op destructor doesn't destroy it.
            TP_LOG("delegating deletion of state to start()");
            op.state_ = nullptr;
        }

        // It's possible that cancellation might have been requested concurrently
        // with deregistration of op.cancelCallback_, although this window is
        // small. We could either ignore this case and just execute set_value()
        // (we are alfter-all already running a callback on the thread-pool and
        // so the 'schedule' operation really succeeded before cancellation was
        // requested. Alternatively we could call set_done() here.
        // Opting for not calling set_done() here as an arbitrary choice since
        // it's less code-gen and one less branch and the likelihood of this
        // eliminating running extra work is low.

        // Do the work.
        work_callback(instance, workContext, work);
    }

    static void CALLBACK work_callback(PTP_CALLBACK_INSTANCE instance, void* workContext, PTP_WORK work) noexcept {
        TP_LOG("running set_value");

        // No cancellation to deal with so just call the receiver.
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

    // This function is only called in the case that the work was successfully cancelled.
    static void CALLBACK cancelled_callback(void* workContext, [[maybe_unused]] void* cleanupContext) noexcept {
        auto& op = *static_cast<type*>(workContext);

        TP_LOG("in cancelled_callback, deregistering stop callback");

        // ??? What context will this method be called on?
        // Will it be called inside the call to CloseThreadpoolCleanupGroupMembers()?
        // Or is it possible it might be called concurrently on some other thread?
        // Docs aren't clear:

        // Deregister the callback before calling set_done() as the call to set_done()
        // will potentially invalidate the stop_token it is registered against.
        op.cancelCallback_.destruct();

        TP_LOG("cancelled_callback flagging as running");

        auto oldState = op.state_->fetch_add(running_flag, std::memory_order_acq_rel);
        if ((oldState & submit_complete_flag) == 0) {
            // start() method has not finished calling SubmitThreadpoolWork() and
            // so we are delegating it the responsiblity to delete the state.
            // Clear the state_ member variable so the op destructor won't free it.
            TP_LOG("cancelled_callback delegated delete of state to start()");
            op.state_ = nullptr;
        }

        // PTP_WORK will be closed by the CloseThreadpoolCleanupGroupMembers()
        // Clear the handle here so the destructor won't try to close the handle.
        op.work_ = nullptr;

        TP_LOG("calling set_done");

        // Resume the receiver with done to signal that it was successfully cancelled. 
        unifex::set_done(std::move(op.receiver_));
    }

    struct request_cancel_callback {
        type& op_;

        void operator()() noexcept {
            op_.request_cancel();
        }
    };

    Receiver receiver_;
    TP_CALLBACK_ENVIRON environ_;
    PTP_CLEANUP_GROUP cleanupGroup_{nullptr};
    PTP_WORK work_{nullptr};
    manual_lifetime<typename stop_token_type_t<Receiver>::template callback_type<request_cancel_callback>> cancelCallback_;

    // Flags set when each of the stages have passed.
    // submit_complete  - set after start() method's call to SubmitThreadpoolWork() has returned
    // cancel_requested - set when stop_requested() is true on the stop_token
    // starting         - set by cancellable_work_callback when thread-pool executes the work

    static constexpr std::uint32_t not_started = 0;
    static constexpr std::uint32_t submit_complete_flag = 1;
    static constexpr std::uint32_t cancel_requested_flag = 2;
    static constexpr std::uint32_t starting_flag = 4; 
    static constexpr std::uint32_t running_flag = 8; 
    
    std::atomic<std::uint32_t>* state_{nullptr};
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
