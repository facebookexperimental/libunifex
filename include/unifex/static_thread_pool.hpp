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

#include <unifex/detail/intrusive_queue.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>

#include <thread>
#include <type_traits>
#include <vector>

namespace unifex {
class static_thread_pool {
  struct task_base {
    task_base *next;
    void (*execute)(task_base *) noexcept;
  };

public:
  static_thread_pool();
  static_thread_pool(std::uint32_t threadCount);
  ~static_thread_pool();

  class scheduler {
    class schedule_sender {
    public:
        template <template<typename...> class Variant,
                  template<typename...> class Tuple>
        using value_types = Variant<Tuple<>>;

        template <template<typename...> class Variant>
        using error_types = Variant<>;
    private:
      template <typename Receiver> class operation : task_base {
        friend schedule_sender;

        static_thread_pool &pool_;
        Receiver receiver_;

        explicit operation(static_thread_pool &pool, Receiver &&r)
            : pool_(pool), receiver_((Receiver &&) r) {
          this->execute = [](task_base * t) noexcept {
            auto &op = *static_cast<operation *>(t);
            if constexpr (!is_stop_never_possible_v<
                              stop_token_type_t<Receiver>>) {
              if (get_stop_token(op.receiver_).stop_requested()) {
                cpo::set_done((Receiver &&) op.receiver_);
                return;
              }
            }
            cpo::set_value((Receiver &&) op.receiver_);
          };
        }

        friend void tag_invoke(tag_t<cpo::start>, operation &op) noexcept {
          op.pool_.enqueue(&op);
        }
      };

      template <typename Receiver>
      friend operation<std::decay_t<Receiver>>
      tag_invoke(tag_t<cpo::connect>, schedule_sender s, Receiver &&r) {
        return operation<std::decay_t<Receiver>>{s.pool_, (Receiver &&) r};
      }

      friend scheduler;

      explicit schedule_sender(static_thread_pool &pool) noexcept
          : pool_(pool) {}

      static_thread_pool &pool_;
    };

    friend schedule_sender tag_invoke(tag_t<cpo::schedule>,
                                      const scheduler &s) noexcept {
      return schedule_sender{s.pool_};
    }

    friend static_thread_pool;
    explicit scheduler(static_thread_pool &pool) noexcept : pool_(pool) {}

    static_thread_pool &pool_;
  };

  scheduler get_scheduler() noexcept { return scheduler{*this}; }

private:
  void run() noexcept;
  void close() noexcept;

  void enqueue(task_base *task) noexcept;

  std::vector<std::thread> threads_;
  std::mutex mut_;
  std::condition_variable cv_;
  bool stop_ = false;

  intrusive_queue<task_base, &task_base::next> queue_;
};

} // namespace unifex
