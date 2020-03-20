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
class timed_single_thread_context;

namespace _timed_single_thread_context {
  using clock_t = std::chrono::steady_clock;
  using time_point = typename clock_t::time_point;

  struct task_base {
    explicit task_base(timed_single_thread_context& context) noexcept
      : context_(&context) {}

    timed_single_thread_context* const context_;
    task_base* next_ = nullptr;
    task_base** prevNextPtr_ = nullptr;
    time_point dueTime_;

    virtual void execute() noexcept = 0;
  };

  class cancel_callback {
    task_base* const task_;
   public:
    explicit cancel_callback(task_base* task) noexcept
      : task_(task) {}

    void operator()() noexcept;
  };

  struct scheduler;

  template <typename Duration>
  struct _schedule_after_sender {
    class type;
  };
  template <typename Duration>
  using schedule_after_sender = typename _schedule_after_sender<Duration>::type;

  template <typename Duration, typename Receiver>
  struct _after_op {
    class type;
  };
  template <typename Duration, typename Receiver>
  using after_operation =
      typename _after_op<Duration, std::remove_cvref_t<Receiver>>::type;

  template <typename Duration, typename Receiver>
  class _after_op<Duration, Receiver>::type final : task_base {
    friend schedule_after_sender<Duration>;

    template <typename Receiver2>
    explicit type(
        timed_single_thread_context& context,
        Duration duration,
        Receiver2&& receiver)
        : task_base(context),
          duration_(duration),
          receiver_((Receiver2 &&) receiver) {
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

   public:
    void start() noexcept;
  };

  template <typename Duration>
  class _schedule_after_sender<Duration>::type {
    friend scheduler;

    explicit type(
        timed_single_thread_context& context,
        Duration duration) noexcept
      : context_(&context), duration_(duration) {}

    timed_single_thread_context* context_;
    Duration duration_;

   public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    template <typename Receiver>
    after_operation<Duration, Receiver> connect(Receiver&& receiver) && {
      return after_operation<Duration, Receiver>{
          *context_, duration_, (Receiver &&) receiver};
    }
    template <typename Receiver>
    after_operation<Duration, Receiver> connect(Receiver&& receiver) & {
      return after_operation<Duration, Receiver>{
          *context_, duration_, (Receiver &&) receiver};
    }
    template <typename Receiver>
    after_operation<Duration, Receiver> connect(Receiver&& receiver) const & {
      return after_operation<Duration, Receiver>{
          *context_, duration_, (Receiver &&) receiver};
    }
  };

  template <typename Receiver>
  struct _at_op {
    class type;
  };
  template <typename Receiver>
  using at_operation = typename _at_op<std::remove_cvref_t<Receiver>>::type;

  template <typename Receiver>
  class _at_op<Receiver>::type final : task_base {
    template <typename Receiver2>
    explicit type(
        timed_single_thread_context& scheduler,
        clock_t::time_point dueTime,
        Receiver2&& receiver)
        : task_base(scheduler)
        , receiver_((Receiver2 &&) receiver) {
      this->dueTime_ = dueTime;
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

    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
        Receiver&>::template callback_type<cancel_callback>>
        cancelCallback_;

   public:
    void start() noexcept;
  };

  class schedule_at_sender {
    friend scheduler;

    explicit schedule_at_sender(
        timed_single_thread_context& context,
        time_point dueTime)
      : context_(&context), dueTime_(dueTime) {}

    timed_single_thread_context* context_;
    time_point dueTime_;
   public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    template <typename Receiver>
    at_operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) {
      return at_operation<std::remove_cvref_t<Receiver>>{
          *context_, dueTime_, (Receiver &&) receiver};
    }
  };

  class scheduler {
    friend timed_single_thread_context;

    explicit scheduler(timed_single_thread_context& context) noexcept
      : context_(&context) {}

    friend bool operator==(const scheduler& a, const scheduler& b) noexcept {
      return a.context_ == b.context_;
    }
    friend bool operator!=(const scheduler& a, const scheduler& b) noexcept {
      return !(a == b);
    }

    timed_single_thread_context* context_;
   public:
    template <typename Rep, typename Ratio>
    auto schedule_after(std::chrono::duration<Rep, Ratio> delay) const noexcept
        -> schedule_after_sender<std::chrono::duration<Rep, Ratio>> {
      return schedule_after_sender<std::chrono::duration<Rep, Ratio>>{
          *context_, delay};
    }

    auto schedule_at(clock_t::time_point dueTime) const noexcept {
      return schedule_at_sender{*context_, dueTime};
    }

    auto schedule() const noexcept {
      return schedule_after(std::chrono::milliseconds{0});
    }
  };
} // namespace _timed_single_thread_context

class timed_single_thread_context {
  using scheduler = _timed_single_thread_context::scheduler;
  using task_base = _timed_single_thread_context::task_base;
  using cancel_callback = _timed_single_thread_context::cancel_callback;
  friend cancel_callback;
  friend scheduler;
  template<typename Duration, typename Receiver>
  friend class _timed_single_thread_context::_after_op;
  template<typename Receiver>
  friend class _timed_single_thread_context::_at_op;

  void enqueue(task_base* task) noexcept;
  void run();

  std::mutex mutex_;
  std::condition_variable cv_;

  // Head of a linked-list in ascending order of due-time.
  task_base* head_ = nullptr;
  bool stop_ = false;

  std::thread thread_;
 public:
  using clock_t = _timed_single_thread_context::clock_t;
  using time_point = _timed_single_thread_context::time_point;

  timed_single_thread_context();
  ~timed_single_thread_context();

  scheduler get_scheduler() noexcept {
    return scheduler{*this};
  }
};

namespace _timed_single_thread_context {
  template <typename Duration, typename Receiver>
  inline void _after_op<Duration, Receiver>::type::start() noexcept {
    this->dueTime_ = clock_t::now() + duration_;
    cancelCallback_.construct(
        get_stop_token(receiver_), cancel_callback{this});
    context_->enqueue(this);
  }

  template <typename Receiver>
  inline void _at_op<Receiver>::type::start() noexcept {
    cancelCallback_.construct(
        get_stop_token(receiver_), cancel_callback{this});
    this->context_->enqueue(this);
  }
} // namespace _timed_single_thread_context
} // namespace unifex
