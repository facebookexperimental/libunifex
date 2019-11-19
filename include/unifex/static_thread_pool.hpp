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

#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/detail/intrusive_queue.hpp>

#include <thread>
#include <type_traits>
#include <vector>
#include <atomic>

namespace unifex
{
  class static_thread_pool {
    struct task_base {
      task_base* next;
      void (*execute)(task_base*) noexcept;
    };

  public:
    static_thread_pool();
    static_thread_pool(std::uint32_t threadCount);
    ~static_thread_pool();

    class scheduler {
      class schedule_sender {
      public:
        template <
            template <typename...>
            class Variant,
            template <typename...>
            class Tuple>
        using value_types = Variant<Tuple<>>;

        template <template <typename...> class Variant>
        using error_types = Variant<>;

      private:
        template <typename Receiver>
        class operation : task_base {
          friend schedule_sender;

          static_thread_pool& pool_;
          Receiver receiver_;

          explicit operation(static_thread_pool& pool, Receiver&& r)
            : pool_(pool)
            , receiver_((Receiver &&) r) {
            this->execute = [](task_base* t) noexcept {
              auto& op = *static_cast<operation*>(t);
              if constexpr (!is_stop_never_possible_v<
                                stop_token_type_t<Receiver>>) {
                if (get_stop_token(op.receiver_).stop_requested()) {
                  unifex::set_done((Receiver &&) op.receiver_);
                  return;
                }
              }
              unifex::set_value((Receiver &&) op.receiver_);
            };
          }

          friend void tag_invoke(tag_t<start>, operation& op) noexcept {
            op.pool_.enqueue(&op);
          }
        };

        template <typename Receiver>
        friend operation<std::decay_t<Receiver>>
        tag_invoke(tag_t<connect>, schedule_sender s, Receiver&& r) {
          return operation<std::decay_t<Receiver>>{s.pool_, (Receiver &&) r};
        }

        friend scheduler;

        explicit schedule_sender(static_thread_pool& pool) noexcept
          : pool_(pool) {}

        static_thread_pool& pool_;
      };

      friend schedule_sender
      tag_invoke(tag_t<schedule>, const scheduler& s) noexcept {
        return schedule_sender{s.pool_};
      }

      friend static_thread_pool;
      explicit scheduler(static_thread_pool& pool) noexcept : pool_(pool) {}

      static_thread_pool& pool_;
    };

    scheduler get_scheduler() noexcept { return scheduler{*this}; }

    void request_stop() noexcept;

  private:
    class thread_state {
    public:
      task_base* try_pop();
      task_base* pop();
      bool try_push(task_base* task);
      void push(task_base* task);
      void request_stop();

    private:
      std::mutex mut_;
      std::condition_variable cv_;
      intrusive_queue<task_base, &task_base::next> queue_;
      bool stopRequested_ = false;
    };

    void run(std::uint32_t index) noexcept;
    void join() noexcept;

    void enqueue(task_base* task) noexcept;

    std::uint32_t threadCount_;
    std::vector<std::thread> threads_;
    std::vector<thread_state> threadStates_;
    std::atomic<std::uint32_t> nextThread_;
  };

}  // namespace unifex
