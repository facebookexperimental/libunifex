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

#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>

#include <condition_variable>
#include <mutex>
#include <type_traits>

namespace unifex {

class manual_event_loop {
  struct task_base {
    task_base* next_ = nullptr;
    virtual void execute() noexcept = 0;
  };

 public:
  class scheduler {
    class schedule_task {
     public:
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types = Variant<Tuple<>>;

      template <template <typename...> class Variant>
      using error_types = Variant<>;

    private:
      friend constexpr blocking_kind tag_invoke(
          tag_t<cpo::blocking>,
          const schedule_task&) noexcept {
        return blocking_kind::never;
      }

      template <typename Receiver>
      class operation final : task_base {
        using stop_token_type = stop_token_type_t<Receiver&>;

       public:
        void start() noexcept {
          loop_->enqueue(this);
        }

       private:
        friend schedule_task;

        template <typename Receiver2>
        explicit operation(Receiver2&& receiver, manual_event_loop* loop)
            : receiver_((Receiver2 &&) receiver), loop_(loop) {}

        void execute() noexcept override {
          if constexpr (is_stop_never_possible_v<stop_token_type>) {
            cpo::set_value(std::move(receiver_));
          } else {
            if (get_stop_token(receiver_).stop_requested()) {
              cpo::set_done(std::move(receiver_));
            } else {
              cpo::set_value(std::move(receiver_));
            }
          }
        }

        UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
        manual_event_loop* const loop_;
      };

    public:
      template <typename Receiver>
      operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
        return operation<std::remove_cvref_t<Receiver>>{(Receiver &&) receiver,
                                                        loop_};
      }

    private:
      friend scheduler;

      explicit schedule_task(manual_event_loop* loop) noexcept
      : loop_(loop)
      {}

      manual_event_loop* const loop_;
    };

    friend manual_event_loop;

    explicit scheduler(manual_event_loop* loop) noexcept : loop_(loop) {}

   public:
    schedule_task schedule() const noexcept {
      return schedule_task{loop_};
    }

   private:
    manual_event_loop* loop_;
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

} // namespace unifex
