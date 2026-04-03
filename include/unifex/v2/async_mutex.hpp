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

#include <unifex/cancellable.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/detail/atomic_intrusive_list.hpp>
#include <unifex/detail/completion_forwarder.hpp>

#include <unifex/detail/prologue.hpp>

#include <atomic>

namespace unifex::v2 {

// Lock-free async mutex.  Cancellation via try_remove.
//
// Uses a Dekker pattern between locked_ and queue_ to
// prevent lost wakeups: seq_cst fences in start() (after
// push, before locked_ exchange) and process_queue() (after
// locked_ release, before empty check) guarantee at least
// one side sees the other.
//
// Unlike async_manual_reset_event, the mutex has no
// concurrent-reset problem: only the lock holder (a single
// thread) transitions locked_ from true to false.
class async_mutex {
  class lock_raw_sender;

public:
  [[nodiscard]] bool try_lock() noexcept;

  [[nodiscard]] auto async_lock() noexcept;

  void unlock() noexcept;

private:
  struct waiter_base : atomic_intrusive_list_node {
    void (*resume_)(waiter_base*) noexcept;
  };

  void process_queue() noexcept;

  atomic_intrusive_list<waiter_base> queue_;
  std::atomic<bool> locked_{false};

  class lock_raw_sender {
  public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    static constexpr bool sends_done = true;
    static constexpr blocking_kind blocking = blocking_kind::maybe;
    static constexpr bool is_always_scheduler_affine = true;

    lock_raw_sender(const lock_raw_sender&) = delete;
    lock_raw_sender(lock_raw_sender&&) = default;

  private:
    friend async_mutex;

    explicit lock_raw_sender(async_mutex& mutex) noexcept : mutex_(mutex) {}

    template <typename Receiver>
    struct _op {
      class type : waiter_base {
        friend lock_raw_sender;

      public:
        explicit type(async_mutex& mutex, Receiver&& r) noexcept
          : mutex_(mutex)
          , receiver_(std::forward<Receiver>(r)) {
          this->resume_ = [](waiter_base* self) noexcept {
            auto* op = static_cast<type*>(self);
            if (try_complete(op)) {
              op->forwardingOp_.start(*op);
            } else {
              // Pop gave us the lock but stop already completed
              // us.  Release the lock to avoid deadlock.
              op->mutex_.unlock();
            }
          };
        }

        type(type&&) = delete;

        Receiver& get_receiver() noexcept { return receiver_; }

        void forward_set_value() noexcept {
          if (cancelled_) {
            unifex::set_done(std::move(receiver_));
          } else {
            unifex::set_value(std::move(receiver_));
          }
        }

        void start() noexcept;
        void stop() noexcept;

      private:
        async_mutex& mutex_;
        Receiver receiver_;
        completion_forwarder<type, Receiver> forwardingOp_;
        bool cancelled_{false};
        bool started_{false};
      };
    };

    template <typename Receiver>
    using operation = typename _op<Receiver>::type;

    template(typename Receiver)                                            //
        (requires receiver_of<Receiver> AND scheduler_provider<Receiver>)  //
        friend operation<Receiver> tag_invoke(
            tag_t<connect>, lock_raw_sender&& s, Receiver&& r) noexcept {
      return operation<Receiver>{s.mutex_, std::forward<Receiver>(r)};
    }

    async_mutex& mutex_;
  };
};

inline auto async_mutex::async_lock() noexcept {
  return cancellable<lock_raw_sender, true>{lock_raw_sender{*this}};
}

inline bool async_mutex::try_lock() noexcept {
  return !locked_.exchange(true, std::memory_order_acquire);
}

template <typename Receiver>
void async_mutex::lock_raw_sender::_op<Receiver>::type::start() noexcept {
  started_ = true;

  if (mutex_.try_lock()) {
    if (try_complete(this)) {
      forwardingOp_.start(*this);
    }
    return;
  }

  // Save ref: after push_back, another thread may pop and
  // complete us, potentially destroying *this.
  async_mutex& mutex = mutex_;

  mutex.queue_.push_back(this);

  // Dekker fence: orders the push before the locked_ exchange.
  std::atomic_thread_fence(std::memory_order_seq_cst);

  if (!mutex.locked_.exchange(true, std::memory_order_acq_rel)) {
    mutex.process_queue();
  }
}

template <typename Receiver>
void async_mutex::lock_raw_sender::_op<Receiver>::type::stop() noexcept {
  if (!started_) {
    // StopsEarly: never enqueued, don't hold the lock.
    cancelled_ = true;
    if (try_complete(this)) {
      forwardingOp_.start(*this);
    }
    return;
  }
  if (mutex_.queue_.try_remove(this)) {
    cancelled_ = true;
    if (try_complete(this)) {
      forwardingOp_.start(*this);
    }
  }
  // else: already popped by process_queue; resume_ handles it.
}

}  // namespace unifex::v2

#include <unifex/detail/epilogue.hpp>
