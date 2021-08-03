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
#include <unifex/stop_token_concepts.hpp>
#include <unifex/detail/intrusive_queue.hpp>

#include <thread>
#include <type_traits>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _static_thread_pool {
  struct task_base {
    task_base* next;
    void (*execute)(task_base*) noexcept;
  };

  template <typename Receiver>
  struct _op {
    class type;
  };
  template <typename Receiver>
  using operation = typename _op<remove_cvref_t<Receiver>>::type;

  class context {
    template <typename Receiver>
    friend struct _op;
  public:
    context();
    context(std::uint32_t threadCount);
    ~context();

    class scheduler {
      template <typename Receiver>
      friend struct _op;
      class schedule_sender {
      public:
        template <
            template <typename...> class Variant,
            template <typename...> class Tuple>
        using value_types = Variant<Tuple<>>;

        template <template <typename...> class Variant>
        using error_types = Variant<>;

        static constexpr bool sends_done = true;

      private:
        template <typename Receiver>
        operation<Receiver> make_operation_(Receiver&& r) const {
          return operation<Receiver>{pool_, (Receiver &&) r};
        }

        template(typename Receiver)
          (requires receiver_of<Receiver>)
        friend operation<Receiver>
        tag_invoke(tag_t<connect>, schedule_sender s, Receiver&& r) {
          return s.make_operation_((Receiver &&) r);
        }

        friend class context::scheduler;

        explicit schedule_sender(context& pool) noexcept
          : pool_(pool) {}

        context& pool_;
      };

      schedule_sender make_sender_() const {
        return schedule_sender{pool_};
      }

      friend schedule_sender
      tag_invoke(tag_t<schedule>, const scheduler& s) noexcept {
        return s.make_sender_();
      }

      friend class context;
      explicit scheduler(context& pool) noexcept
        : pool_(pool) {}

      friend bool operator==(scheduler a, scheduler b) noexcept {
        return &a.pool_ == &b.pool_;
      }
      friend bool operator!=(scheduler a, scheduler b) noexcept {
        return &a.pool_ != &b.pool_;
      }

      context& pool_;
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

  template <typename Receiver>
  class _op<Receiver>::type : task_base {
    friend context::scheduler::schedule_sender;

    context& pool_;
    Receiver receiver_;

    explicit type(context& pool, Receiver&& r)
      : pool_(pool)
      , receiver_((Receiver &&) r) {
      this->execute = [](task_base* t) noexcept {
        auto& op = *static_cast<type*>(t);
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

    void enqueue_(task_base* op) const {
      pool_.enqueue(op);
    }

    friend void tag_invoke(tag_t<start>, type& op) noexcept {
      op.enqueue_(&op);
    }
  };

} // _static_thread_pool

using static_thread_pool = _static_thread_pool::context;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
