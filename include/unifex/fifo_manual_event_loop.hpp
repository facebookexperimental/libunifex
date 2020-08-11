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

// TODO: At the moment I don't think this is necessary.
// fifo_bulk_schedule should be removed and collapsed into this
// as the result of a tag_invoke call to bulk_schedule

#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/fifo_support.hpp>
#include <unifex/bulk_schedule.hpp>

#include <condition_variable>
#include <mutex>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

#include <iostream>

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


namespace _fifo_manual_event_loop {
class context;

struct task_base {
  task_base* next_ = nullptr;
  virtual void execute() noexcept = 0;
};

template <typename Receiver>
struct _op {
  class type;
};
template <typename Receiver>
using operation = typename _op<remove_cvref_t<Receiver>>::type;

template <typename Receiver>
class _op<Receiver>::type final : task_base {
  using stop_token_type = stop_token_type_t<Receiver&>;

 public:
  template <typename Receiver2>
  explicit type(Receiver2&& receiver, context* loop)
    : receiver_((Receiver2 &&) receiver), loop_(loop) {}

  void start() noexcept;

 private:
  void execute() noexcept override {
    if constexpr (is_stop_never_possible_v<stop_token_type>) {
      unifex::set_value(std::move(receiver_));
    } else {
      if (get_stop_token(receiver_).stop_requested()) {
        unifex::set_done(std::move(receiver_));
      } else {
        unifex::set_value(std::move(receiver_));
      }
    }
  }

  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  context* const loop_;
};

class context {
  template <class Receiver>
  friend struct _op;
 public:
  class scheduler {
    class schedule_task {
      friend constexpr blocking_kind tag_invoke(
          tag_t<blocking>,
          const schedule_task&) noexcept {
        return blocking_kind::never;
      }

     public:
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types = Variant<Tuple<>>;

      template <template <typename...> class Variant>
      using error_types = Variant<>;

      static constexpr bool sends_done = true;

      template <typename Receiver>
      operation<Receiver> connect(Receiver&& receiver) const& {
        std::cout << "Connecting fifo_manual_event_loop to a receiver. get_fifo_context(receiver): " << unifex::get_fifo_context(receiver) << "\n";
        return operation<Receiver>{(Receiver &&) receiver, loop_};
      }

      // FIFO_CHANGES: This is a fifo context, return its loop_ as an id
      friend void* tag_invoke(tag_t<get_fifo_context>, schedule_task& task) noexcept {
        return task.loop_;
      }

    private:
      friend scheduler;

      explicit schedule_task(context* loop) noexcept
        : loop_(loop)
      {}

      context* const loop_;
    };

    friend context;

    explicit scheduler(context* loop) noexcept : loop_(loop) {}

   public:
    schedule_task schedule() const noexcept {
      return schedule_task{loop_};
    }

    friend bool operator==(scheduler a, scheduler b) noexcept {
      return a.loop_ == b.loop_;
    }
    friend bool operator!=(scheduler a, scheduler b) noexcept {
      return a.loop_ != b.loop_;
    }

    // FIFO_CHANGES: This is a fifo context, return its loop_ as an id
    friend void* tag_invoke(tag_t<get_fifo_context>, scheduler& sched) noexcept {
      return sched.loop_;
    }

    template<typename Integral>
    friend auto tag_invoke(tag_t<bulk_schedule>, scheduler& s, Integral n) noexcept {
      return _fifo_bulk_schedule::default_sender<scheduler, Integral>{
        s, std::move(n)};
    }

   private:
    context* loop_;
  };

  scheduler get_scheduler() {
    return scheduler{this};
  }

  void run();

  void stop();

 private:
  void enqueue(task_base* task);

  std::mutex mutex_;
  std::condition_variable cv_;
  task_base* head_ = nullptr;
  task_base* tail_ = nullptr;
  bool stop_ = false;
};

template <typename Receiver>
inline void _op<Receiver>::type::start() noexcept {
  std::cout << "start() on manual event loop operation\n";
  loop_->enqueue(this);
  std::cout << "After enqueue\n";
  // FIFO_CHANGES: If the work is due to be submitted on this context,
  // enqueue it as well
  if(get_fifo_context(receiver_) == loop_) {
    std::cout << "start() trying eager enqueue\n";
    auto started = start_eagerly(receiver_);
    if(started) {
      std::cout << "Work started eagerly\n";
    }
  }
}

} // namespace _fifo_manual_event_loop


using fifo_manual_event_loop = _fifo_manual_event_loop::context;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
