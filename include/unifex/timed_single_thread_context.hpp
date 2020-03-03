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
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <type_traits>

namespace unifex {

class timed_single_thread_context {
 public:
  using clock_t = std::chrono::steady_clock;
  using time_point = typename clock_t::time_point;

 private:
  struct task_base {
    explicit task_base(timed_single_thread_context* context) noexcept
        : context_(context) {
      assert(context_ != nullptr);
    }

    timed_single_thread_context* const context_;
    task_base* next_ = nullptr;
    task_base** prevNextPtr_ = nullptr;
    time_point dueTime_;

    virtual void execute() noexcept = 0;
  };

  class cancel_callback {
   public:
    explicit cancel_callback(task_base* task) noexcept : task_(task) {}

    void operator()() noexcept;

   private:
    task_base* const task_;
  };

 public:
  class scheduler {
    template <typename Duration>
    class schedule_after_sender {
     public:
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types = Variant<Tuple<>>;

      template <template <typename...> class Variant>
      using error_types = Variant<>;

     private:
      template <typename Receiver>
      class operation final : task_base {
       public:
        void start() noexcept {
          this->dueTime_ = clock_t::now() + duration_;
          cancelCallback_.construct(
              get_stop_token(receiver_), cancel_callback{this});
          context_->enqueue(this);
        }

       private:
        friend schedule_after_sender;

        template <typename Receiver2>
        explicit operation(
            timed_single_thread_context* context,
            Duration duration,
            Receiver2&& receiver)
            : task_base(context),
              duration_(duration),
              receiver_((Receiver2 &&) receiver) {
          assert(context != nullptr);
          assert(context_ != nullptr);
        }

        void execute() noexcept final {
          cancelCallback_.destruct();
          if constexpr (is_stop_never_possible_v<
                            stop_token_type_t<Receiver&>>) {
            unifex::set_value(static_cast<Receiver&&>(receiver_));
          } else {
            if (get_stop_token(receiver_).stop_requested()) {
              unifex::set_done(static_cast<Receiver&&>(receiver_));
            } else {
              unifex::set_value(static_cast<Receiver&&>(receiver_));
            }
          }
        }

        Duration duration_;
        UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
        UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
            Receiver&>::template callback_type<cancel_callback>>
            cancelCallback_;
      };

     public:
      template <typename Receiver>
      operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
        return operation<std::remove_cvref_t<Receiver>>{
            context_, duration_, (Receiver &&) receiver};
      }
      template <typename Receiver>
      operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) & {
        return operation<std::remove_cvref_t<Receiver>>{
            context_, duration_, (Receiver &&) receiver};
      }
      template <typename Receiver>
      operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) const & {
        return operation<std::remove_cvref_t<Receiver>>{
            context_, duration_, (Receiver &&) receiver};
      }

     private:
      friend scheduler;

      explicit schedule_after_sender(
          timed_single_thread_context* context,
          Duration duration) noexcept
          : context_(context), duration_(duration) {}

      timed_single_thread_context* context_;
      Duration duration_;
    };

    class schedule_at_sender {
     public:
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types = Variant<Tuple<>>;

      template <template <typename...> class Variant>
      using error_types = Variant<>;

     private:
      template <typename Receiver>
      class operation : task_base {
       public:
        void start() noexcept {
          cancelCallback_.construct(
              get_stop_token(receiver_), cancel_callback{this});
          this->context_->enqueue(this);
        }

       private:
        template <typename Receiver2>
        explicit operation(
            timed_single_thread_context* scheduler,
            clock_t::time_point dueTime,
            Receiver2&& receiver)
            : task_base(scheduler), receiver_((Receiver2 &&) receiver) {
          this->dueTime_ = dueTime;
        }

        void execute() noexcept final {
          cancelCallback_.destruct();
          if constexpr (is_stop_never_possible_v<
                            stop_token_type_t<Receiver&>>) {
            unifex::set_value(
                static_cast<Receiver&&>(receiver_), scheduler{this->context_});
          } else {
            if (get_stop_token(receiver_).stop_requested()) {
              unifex::set_done(static_cast<Receiver&&>(receiver_));
            } else {
              unifex::set_value(static_cast<Receiver&&>(receiver_));
            }
          }
        }

        UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
        UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
            Receiver&>::template callback_type<cancel_callback>>
            cancelCallback_;
      };

     public:
      template <typename Receiver>
      operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) {
        return operation<std::remove_cvref_t<Receiver>>{
            context_, dueTime_, (Receiver &&) receiver};
      }

     private:
      friend scheduler;

      explicit schedule_at_sender(
          timed_single_thread_context* context,
          time_point dueTime)
          : context_(context), dueTime_(dueTime) {}

      timed_single_thread_context* context_;
      time_point dueTime_;
    };

    friend bool operator==(const scheduler& a, const scheduler& b) noexcept {
      return a.context_ == b.context_;
    }
    friend bool operator!=(const scheduler& a, const scheduler& b) noexcept {
      return !(a == b);
    }

   public:
    template <typename Rep, typename Ratio>
    auto schedule_after(std::chrono::duration<Rep, Ratio> delay) const noexcept {
      return schedule_after_sender<std::chrono::duration<Rep, Ratio>>{
          context_, delay};
    }

    auto schedule_at(clock_t::time_point dueTime) const noexcept {
      return schedule_at_sender{context_, dueTime};
    }

    auto schedule() const noexcept {
      return schedule_after(std::chrono::milliseconds{0});
    }

   private:
    friend timed_single_thread_context;

    explicit scheduler(timed_single_thread_context* context) noexcept
        : context_(context) {
      assert(context_ != nullptr);
    }

    timed_single_thread_context* context_;
  };

  timed_single_thread_context();

  ~timed_single_thread_context();

  scheduler get_scheduler() noexcept {
    return scheduler{this};
  }

 private:
  void enqueue(task_base* task) noexcept;
  void run();

  std::mutex mutex_;
  std::condition_variable cv_;

  // Head of a linked-list in ascending order of due-time.
  task_base* head_ = nullptr;
  bool stop_ = false;

  std::thread thread_;
};

} // namespace unifex
