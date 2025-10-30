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

#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/detail/completion_forwarder.hpp>
#include <unifex/detail/intrusive_list.hpp>

#include <unifex/detail/prologue.hpp>

#include <atomic>
#include <mutex>
#include <optional>

namespace unifex::v2 {

class async_mutex {
  class lock_sender;

public:
  [[nodiscard]] bool try_lock() noexcept;

  [[nodiscard]] lock_sender async_lock() noexcept;

  void unlock() noexcept;

private:
  struct waiter_base {
    bool enqueued() const noexcept {
      return next_ != this;
    }

    void set_dequeued() noexcept {
      next_ = this;
    }

    void (*resume_)(waiter_base*) noexcept;
    waiter_base* next_{this};
    waiter_base* prev_;
  };

  class lock_sender {
  public:
    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    static constexpr bool sends_done = true;
    static constexpr blocking_kind blocking = blocking_kind::maybe;
    static constexpr bool is_always_scheduler_affine = true;

    lock_sender(const lock_sender&) = delete;
    lock_sender(lock_sender&&) = default;

  private:
    friend async_mutex;

    explicit lock_sender(async_mutex& mutex) noexcept : mutex_(mutex) {}

    template <typename Receiver>
    struct _op {
      class type : waiter_base {
        friend lock_sender;

      public:
        explicit type(async_mutex& mutex, Receiver&& r) noexcept 
        : mutex_(mutex)
        , receiver_(std::forward<Receiver>(r)) {
          this->resume_ = [](waiter_base* self) noexcept {
            static_cast<type*>(self)->set_value();
          };
        }

        type(type&&) = delete;

        Receiver& get_receiver() noexcept {
          return receiver_;
        }

        void forward_set_value() noexcept {
          if (enqueued_state_.load(std::memory_order_acquire) ==
                enqueued_state::kCancelled) {
            unifex::set_done(std::move(receiver_));
          } else {
            unifex::set_value(std::move(receiver_));
          }
        }

        void start() noexcept;        

      private:
        enum class enqueued_state : uint8_t {
          kNotEnqueued,
          kLocking,
          kLockedButNotEnqueued,
          kEnqueued,
          kUnlocked,
          kCancelled
        };

        void set_done() noexcept;
        void set_value() noexcept;

        struct stop_callback final {
          type* self;

          void operator()() noexcept;
        };

        using stop_callback_type = typename stop_token_type_t<Receiver>
          ::template callback_type<stop_callback>;

        async_mutex& mutex_;
        Receiver receiver_;
        completion_forwarder<type, Receiver> forwardingOp_;
        std::optional<stop_callback_type> stop_callback_;
        std::atomic<enqueued_state> enqueued_state_{
            enqueued_state::kNotEnqueued};
      };
    };

    template <typename Receiver>
    using operation = typename _op<Receiver>::type;

    template(typename Receiver)                                        //
    (requires receiver_of<Receiver> AND scheduler_provider<Receiver>)  //
    friend operation<Receiver> tag_invoke(
        tag_t<connect>, lock_sender&& s, Receiver&& r) noexcept {      
      return operation<Receiver>{s.mutex_, std::forward<Receiver>(r)};
    }

    async_mutex& mutex_;
  };

  // Attempt to enqueue the waiter object to the queue.
  // Returns true if successfully enqueued, false if it was not enqueued because
  // the lock was acquired synchronously.
  bool try_enqueue(waiter_base* waiter) noexcept;
  // Returns false if waiter was not in the queue.
  bool try_dequeue(waiter_base* waiter) noexcept;

  intrusive_list<waiter_base, &waiter_base::next_, &waiter_base::prev_> queue_;
  std::atomic<bool> locked_{false};
  std::mutex mutex_;
};

inline async_mutex::lock_sender async_mutex::async_lock() noexcept {
  return lock_sender{*this};
}

inline bool async_mutex::try_lock() noexcept {
  return !locked_.exchange(true);
}

template <typename Receiver>
void async_mutex::lock_sender::_op<Receiver>::type::start() noexcept {
  stop_callback_.emplace(get_stop_token(receiver_), stop_callback{this});
  if (enqueued_state_.exchange(enqueued_state::kLocking, std::memory_order_acq_rel)
      == enqueued_state::kCancelled) {
    // stop callback ran synchronously
    stop_callback_.reset();
    enqueued_state_.store(enqueued_state::kCancelled, std::memory_order_release);
    forwardingOp_.start(*this);
    return;
  }
  
  // if this returns true then we've opened ourselves up to an unlocker
  // resuming us!
  bool enqueued{mutex_.try_enqueue(this)};
  
  auto oldState = enqueued_state::kLocking;
  auto newState = enqueued ? enqueued_state::kEnqueued
                           : enqueued_state::kLockedButNotEnqueued;
  
  if (enqueued_state_.compare_exchange_strong(
      oldState, newState, std::memory_order_acq_rel)) {
    // successfully transitioned locking -> enqueued|locked
    if (enqueued) {
      // we need to return here; the next state transition will happen when an unlocker
      // dequeues us or the stop request fires, both of which could be happening
      // concurrently to this code so it's unsafe to touch any members at this point
      return;
    } else {
      // we took the lock synchronously, so no unlocker will wake us up, but the stop
      // request could run
      stop_callback_.reset();
      assert([this]() noexcept {
        auto state = enqueued_state_.load(std::memory_order_acquire);
        return state == enqueued_state::kCancelled ||
            state == enqueued_state::kLockedButNotEnqueued;
      }());
    }
  } else {
    // we've been completed concurrently while trying to set up
    stop_callback_.reset();    
    if (oldState == enqueued_state::kCancelled) {
      // the stop request ran; we have to handle the possibility that we were enqueued onto
      // the list of waiters and then popped by an unlocker
      if (!enqueued || !mutex_.try_dequeue(this)) {
        // we got the lock but don't intend to keep it so give it back
        mutex_.unlock();
      }
    } else {
      // we were enqueued but an unlocker dequeued us, giving us the lock but not completing
      assert(oldState == enqueued_state::kUnlocked);
      assert(enqueued);
    }
  }
  // Complete with value or done, depending on enqueued_state_ == kCancelled
  forwardingOp_.start(*this);
}

template <typename Receiver>
void async_mutex::lock_sender::_op<Receiver>::type::stop_callback::operator()() noexcept {
  self->set_done();
}

template <typename Receiver>
void async_mutex::lock_sender::_op<Receiver>::type::set_done() noexcept {
  auto oldState = enqueued_state_.load(std::memory_order_relaxed);

  do {
    if (oldState == enqueued_state::kUnlocked) {
      // the stop request ran after we acquired the lock
      return;
    }
  } while (!enqueued_state_.compare_exchange_weak(
      oldState, enqueued_state::kCancelled, std::memory_order_acq_rel));

  // we transitioned to cancelled; whether there's more to do depends on what from
  switch (oldState) {
    case enqueued_state::kUnlocked:
    default:
      // invalid state
      std::terminate();

    case enqueued_state::kNotEnqueued:
    case enqueued_state::kLocking:
    case enqueued_state::kLockedButNotEnqueued:
      // start() is still running
      return;

    case enqueued_state::kEnqueued:
      // start() put us in the queue
      if (!mutex_.try_dequeue(this)) {
        // an unlocker gave us the lock by popping us from the queue
        mutex_.unlock();
      }
      stop_callback_.reset();
      forwardingOp_.start(*this);
    }
}

template <typename Receiver>
void async_mutex::lock_sender::_op<Receiver>::type::set_value() noexcept {
  // an unlocker has popped us from the mutex's queue
  auto oldState = enqueued_state_.load(std::memory_order_relaxed);

  do {
    if (oldState == enqueued_state::kCancelled) {
      // set_done is running; let it run
      return;
    }
  } while (!enqueued_state_.compare_exchange_weak(
      oldState, enqueued_state::kUnlocked, std::memory_order_acq_rel));

  // we transitioned to unlocked; whether there's more to do depends on what from
  if (oldState == enqueued_state::kLocking) {
    // start() is still running
    return;
  } else if (oldState == enqueued_state::kEnqueued) {
    // start() has returned or will soon and we beat the stop request to updating state
    stop_callback_.reset();
    forwardingOp_.start(*this);
  } else {
    // invalid state
    std::terminate();
  }
}

}  // namespace unifex::v2

#include <unifex/detail/epilogue.hpp>
