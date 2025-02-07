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

#include <unifex/async_mutex.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/detail/prologue.hpp>

#include <list>
#include <utility>

namespace unifex {

class async_shared_mutex {
  class unique_lock_sender;
  class shared_lock_sender;

public:
  async_shared_mutex() noexcept;
  async_shared_mutex(const async_shared_mutex&) = delete;
  async_shared_mutex(async_shared_mutex&&) = delete;
  ~async_shared_mutex();

  async_shared_mutex& operator=(const async_shared_mutex&) = delete;
  async_shared_mutex& operator=(async_shared_mutex&&) = delete;

  [[nodiscard]] bool try_lock() noexcept;
  [[nodiscard]] bool try_lock_shared() noexcept;

  [[nodiscard]] unique_lock_sender async_lock() noexcept;
  [[nodiscard]] shared_lock_sender async_lock_shared() noexcept;

  void unlock() noexcept;
  void unlock_shared() noexcept;

private:
  struct waiter_base {
    void (*resume_)(waiter_base*) noexcept;
    waiter_base* next_;
  };

  class unique_lock_sender {
  public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    static constexpr bool sends_done = false;

    static constexpr blocking_kind blocking = blocking_kind::maybe;

    static constexpr bool is_always_scheduler_affine = false;

    unique_lock_sender(const unique_lock_sender&) = delete;
    unique_lock_sender(unique_lock_sender&&) = default;

  private:
    friend async_shared_mutex;

    explicit unique_lock_sender(async_shared_mutex& mutex) noexcept
      : mutex_(mutex) {}

    template <typename Receiver>
    struct _op {
      class type : waiter_base {
        friend unique_lock_sender;

      public:
        template <typename Receiver2>
        explicit type(async_shared_mutex& mutex, Receiver2&& r) noexcept
          : mutex_(mutex)
          , receiver_((Receiver2&&)r) {
          this->resume_ = [](waiter_base* self) noexcept {
            type& op = *static_cast<type*>(self);
            unifex::set_value((Receiver&&)op.receiver_);
          };
        }

        type(type&&) = delete;

      private:
        friend void tag_invoke(tag_t<start>, type& op) noexcept {
          if (!op.try_enqueue()) {
            set_value((Receiver&&)op.receiver_);
          }
        }

        bool try_enqueue() noexcept { return mutex_.try_enqueue(this, true); }

        async_shared_mutex& mutex_;
        Receiver receiver_;
      };
    };
    template <typename Receiver>
    using operation = typename _op<remove_cvref_t<Receiver>>::type;

    template(typename Receiver)        //
        (requires receiver<Receiver>)  //
        friend operation<Receiver> tag_invoke(
            tag_t<connect>, unique_lock_sender&& s, Receiver&& r) noexcept {
      return operation<Receiver>{s.mutex_, (Receiver&&)r};
    }

    async_shared_mutex& mutex_;
  };

  class shared_lock_sender {
  public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    static constexpr bool sends_done = false;

    // we complete inline if we manage to grab the lock immediately
    static constexpr blocking_kind blocking = blocking_kind::maybe;

    // if we have to wait for the lock, we'll be resumed on whichever scheduler
    // happens to be running the unlock()
    static constexpr bool is_always_scheduler_affine = false;

    shared_lock_sender(const shared_lock_sender&) = delete;
    shared_lock_sender(shared_lock_sender&&) = default;

  private:
    friend async_shared_mutex;

    explicit shared_lock_sender(async_shared_mutex& mutex) noexcept
      : mutex_(mutex) {}

    template <typename Receiver>
    struct _op {
      class type : waiter_base {
        friend shared_lock_sender;

      public:
        template <typename Receiver2>
        explicit type(async_shared_mutex& mutex, Receiver2&& r) noexcept
          : mutex_(mutex)
          , receiver_((Receiver2&&)r) {
          this->resume_ = [](waiter_base* self) noexcept {
            type& op = *static_cast<type*>(self);
            unifex::set_value((Receiver&&)op.receiver_);
          };
        }

        type(type&&) = delete;

      private:
        friend void tag_invoke(tag_t<start>, type& op) noexcept {
          if (!op.try_enqueue()) {
            // Failed to enqueue because we acquired the lock
            // synchronously. Invoke the continuation inline
            // without type-erasure here.
            set_value((Receiver&&)op.receiver_);
          }
        }

        bool try_enqueue() noexcept { return mutex_.try_enqueue(this, false); }

        async_shared_mutex& mutex_;
        Receiver receiver_;
      };
    };
    template <typename Receiver>
    using operation = typename _op<remove_cvref_t<Receiver>>::type;

    template(typename Receiver)        //
        (requires receiver<Receiver>)  //
        friend operation<Receiver> tag_invoke(
            tag_t<connect>, shared_lock_sender&& s, Receiver&& r) noexcept {
      return operation<Receiver>{s.mutex_, (Receiver&&)r};
    }

    async_shared_mutex& mutex_;
  };

  // Attempt to enqueue the waiter object to the queue.
  // Returns true if successfully enqueued, false if it was not enqueued because
  // the lock was acquired synchronously.
  bool try_enqueue(waiter_base* waiter, bool unique) noexcept;

  async_mutex mutex_;
  int activeUniqueCount_;
  int activeSharedCount_;
  std::list<std::pair<waiter_base*, bool>> pendingQueue_;
};

inline bool async_shared_mutex::try_lock() noexcept {
  unifex::sync_wait(mutex_.async_lock());
  if (activeUniqueCount_ == 0 && activeSharedCount_ == 0) {
    UNIFEX_ASSERT(pendingQueue_.empty());
    activeUniqueCount_++;
    mutex_.unlock();
    return true;
  }
  mutex_.unlock();
  return false;
}

inline bool async_shared_mutex::try_lock_shared() noexcept {
  unifex::sync_wait(mutex_.async_lock());
  if (activeUniqueCount_ == 0 && pendingQueue_.empty()) {
    activeSharedCount_++;
    mutex_.unlock();
    return true;
  }
  mutex_.unlock();
  return false;
}

inline async_shared_mutex::unique_lock_sender
async_shared_mutex::async_lock() noexcept {
  return unique_lock_sender{*this};
}

inline async_shared_mutex::shared_lock_sender
async_shared_mutex::async_lock_shared() noexcept {
  return shared_lock_sender{*this};
}

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
