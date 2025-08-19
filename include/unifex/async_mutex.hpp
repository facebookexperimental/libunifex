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
#include <unifex/detail/intrusive_list.hpp>

#include <unifex/detail/prologue.hpp>

#include <atomic>
#include <mutex>
#include <optional>

namespace unifex {

class async_mutex {
  class lock_sender;

public:
  [[nodiscard]] bool try_lock() noexcept;

  [[nodiscard]] lock_sender async_lock() noexcept;

  void unlock() noexcept;

private:
  struct waiter_base {
    void (*resume_)(waiter_base*) noexcept;
    waiter_base* next_;
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

    template <typename WrappedOp, typename StopToken>
    struct _op {
      class type : waiter_base {
        friend lock_sender;

      public:
        template <typename Receiver>
        explicit type(async_mutex& mutex, Receiver&& r) noexcept
          : mutex_(mutex)
          , stop_token_(get_stop_token(r))
          , wrappedOp_(connect(schedule(), std::forward<Receiver>(r))) {
          this->resume_ = [](waiter_base* self) noexcept {
            static_cast<type*>(self)->set_value();
          };
        }

        type(type&&) = delete;

      private:
        enum class enqueued_state : uint8_t {
          kNotEnqueued,
          kLockedButNotEnqueued,
          kEnqueued,
          kCancelled
        };

        void start() noexcept;
        void set_done() noexcept;
        void set_value() noexcept;

        struct stop_callback final {
          type* self;

          void operator()() noexcept;
        };

        friend void tag_invoke(tag_t<unifex::start>, type& op) noexcept {
          op.start();
        }

        using stop_callback_type =
            typename StopToken::template callback_type<stop_callback>;

        async_mutex& mutex_;
        StopToken stop_token_;
        WrappedOp wrappedOp_;
        std::optional<stop_callback_type> stop_callback_;
        std::once_flag completed_;
        std::atomic<enqueued_state> enqueued_state_{
            enqueued_state::kNotEnqueued};
      };
    };

    template <typename Receiver>
    using operation = typename _op<
        connect_result_t<decltype(schedule()), Receiver>,
        stop_token_type_t<Receiver>>::type;

    template(typename Receiver)                                            //
        (requires receiver_of<Receiver> AND scheduler_provider<Receiver>)  //
        friend operation<Receiver> tag_invoke(
            tag_t<connect>, lock_sender&& s, Receiver&& r) noexcept {
      return operation<Receiver>{s.mutex_, std::forward<Receiver>(r)};
    }

    async_mutex& mutex_;
  };

  bool try_enqueue(waiter_base* waiter) noexcept;
  void dequeue(waiter_base* waiter) noexcept;

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

template <typename WrappedOp, typename StopToken>
void async_mutex::lock_sender::_op<WrappedOp, StopToken>::type::
    start() noexcept {
  stop_callback_.emplace(stop_token_, stop_callback{this});
  bool enqueued{mutex_.try_enqueue(this)};
  enqueued_state old_state{enqueued_state_.exchange(
      enqueued ? enqueued_state::kEnqueued
               : enqueued_state::kLockedButNotEnqueued)};
  if (old_state == enqueued_state::kCancelled) {
    if (!enqueued) {
      mutex_.unlock();
    } else {
      mutex_.dequeue(this);
    }
    // Stop notification already happened, clean up here
    stop_callback_.reset();
    unifex::start(wrappedOp_);  // aka set_done(receiver)
  } else if (!enqueued) {
    set_value();
  }
}

template <typename WrappedOp, typename StopToken>
void async_mutex::lock_sender::_op<WrappedOp, StopToken>::type::stop_callback::
operator()() noexcept {
  self->set_done();
}

template <typename WrappedOp, typename StopToken>
void async_mutex::lock_sender::_op<WrappedOp, StopToken>::type::
    set_done() noexcept {
  std::call_once(completed_, [this]() noexcept {
    enqueued_state old_state{
        enqueued_state_.exchange(enqueued_state::kCancelled)};
    if (old_state == enqueued_state::kLockedButNotEnqueued) {
      mutex_.unlock();
    } else if (old_state == enqueued_state::kEnqueued) {
      mutex_.dequeue(this);
    } else {
      // start() is not done yet, let it clean up
      return;
    }
    stop_callback_.reset();
    unifex::start(wrappedOp_);  // aka set_done(receiver)
  });
}

template <typename WrappedOp, typename StopToken>
void async_mutex::lock_sender::_op<WrappedOp, StopToken>::type::
    set_value() noexcept {
  std::call_once(completed_, [this]() noexcept {
    stop_callback_.reset();
    unifex::start(wrappedOp_);  // aka set_value(receiver)
  });
}

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
