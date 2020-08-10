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

#include <condition_variable>
#include <mutex>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

#include <iostream>

namespace unifex {
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
