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
#include <unifex/sender_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>

#include <cassert>
#include <chrono>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace unifex {

class thread_unsafe_event_loop {
 public:
  using clock_t = std::chrono::steady_clock;
  using time_point_t = clock_t::time_point;

 private:
  class operation_base {
   protected:
    operation_base(thread_unsafe_event_loop& loop) noexcept : loop_(loop) {}

    operation_base(const operation_base&) = delete;
    operation_base(operation_base&&) = delete;

   public:
    void start() noexcept {
      loop_.enqueue(this);
    }

   private:
    friend thread_unsafe_event_loop;

    virtual void execute() noexcept = 0;

    thread_unsafe_event_loop& loop_;
    operation_base* next_;
    operation_base** prevPtr_;

   protected:
    time_point_t dueTime_;
  };

  class cancel_callback {
   public:
    explicit cancel_callback(operation_base& op) noexcept : op_(&op) {}

    void operator()() noexcept;

   private:
    operation_base* const op_;
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
      class operation final : public operation_base {
       public:
        void start() noexcept {
          this->dueTime_ = clock_t::now() + duration_;
          callback_.construct(
              get_stop_token(receiver_), cancel_callback{*this});
          operation_base::start();
        }

       private:
        friend schedule_after_sender;

        template <typename Receiver2>
        explicit operation(
            Receiver2&& r,
            Duration d,
            thread_unsafe_event_loop& loop)
            : operation_base(loop), receiver_((Receiver2 &&) r), duration_(d) {}

        void execute() noexcept override {
          callback_.destruct();
          if constexpr (is_stop_never_possible_v<
                            stop_token_type_t<Receiver&>>) {
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
        UNIFEX_NO_UNIQUE_ADDRESS Duration duration_;
        UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
            Receiver&>::template callback_type<cancel_callback>>
            callback_;
      };

     public:
      template <typename Receiver>
      operation<std::remove_cvref_t<Receiver>> connect(Receiver&& r) && {
        return operation<std::remove_cvref_t<Receiver>>{
            (Receiver &&) r, duration_, loop_};
      }

     private:
      friend scheduler;

      explicit schedule_after_sender(
          thread_unsafe_event_loop& loop,
          Duration duration) noexcept
          : loop_(loop), duration_(duration) {}

      thread_unsafe_event_loop& loop_;
      Duration duration_;
    };

    struct schedule_at_sender {
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types = Variant<Tuple<>>;

      template <template <typename...> class Variant>
      using error_types = Variant<>;

     private:
      template <typename Receiver>
      class operation final : public operation_base {
       public:
        void start() noexcept {
          callback_.construct(
              get_stop_token(receiver_), cancel_callback{*this});
          operation_base::start();
        }

       private:
        friend schedule_at_sender;

        template <typename Receiver2>
        explicit operation(
            Receiver2&& r,
            time_point_t tp,
            thread_unsafe_event_loop& loop)
            : operation_base(loop), receiver_((Receiver2 &&) r) {
          this->dueTime_ = tp;
        }

        void execute() noexcept override {
          callback_.destruct();
          if constexpr (is_stop_never_possible_v<
                            stop_token_type_t<Receiver&>>) {
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
        UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
            Receiver&>::template callback_type<cancel_callback>>
            callback_;
      };

     public:
      template <typename Receiver>
      operation<std::remove_cvref_t<Receiver>> connect(Receiver&& r) && {
        return operation<std::remove_cvref_t<Receiver>>{
            (Receiver &&) r, dueTime_, loop_};
      }

     private:
      friend scheduler;

      explicit schedule_at_sender(
          thread_unsafe_event_loop& loop,
          time_point_t dueTime)
          : loop_(loop), dueTime_(dueTime) {}

      thread_unsafe_event_loop& loop_;
      time_point_t dueTime_;
    };

   public:
    auto schedule_at(time_point_t dueTime) const noexcept {
      return schedule_at_sender{loop_, dueTime};
    }

    template <typename Rep, typename Ratio>
    auto schedule_after(std::chrono::duration<Rep, Ratio> d) const noexcept {
      return schedule_after_sender<std::chrono::duration<Rep, Ratio>>{loop_, d};
    }

    auto schedule() const noexcept {
      return schedule_after(std::chrono::milliseconds(0));
    }

   private:
    friend thread_unsafe_event_loop;

    explicit scheduler(thread_unsafe_event_loop& loop) noexcept : loop_(loop) {}

    thread_unsafe_event_loop& loop_;
  };

  scheduler get_scheduler() noexcept {
    return scheduler{*this};
  }

 private:
  void enqueue(operation_base* op) noexcept;

  template <typename T, typename StopToken>
  class sync_wait_promise {
    enum class state { incomplete, done, value, error };

    class receiver {
     public:
      template <typename... Values>
          void set_value(Values&&... values) && noexcept {
        try {
          promise_.value_.construct((Values &&) values...);
          promise_.state_ = state::value;
        } catch (...) {
          promise_.exception_.construct(std::current_exception());
          promise_.state_ = state::error;
        }
      }

      void set_error(std::exception_ptr ex) && noexcept {
        promise_.exception_.construct(std::move(ex));
        promise_.state_ = state::error;
      }

      void set_done() && noexcept {
        promise_.state_ = state::done;
      }

      friend const StopToken& tag_invoke(
          tag_t<get_stop_token>,
          const receiver& r) noexcept {
        return r.get_stop_token();
      }

     private:
      friend sync_wait_promise;

      StopToken& get_stop_token() const noexcept {
        return promise_.stopToken_;
      }

      explicit receiver(sync_wait_promise& promise) noexcept
          : promise_(promise) {}

      sync_wait_promise& promise_;
    };

   public:
    explicit sync_wait_promise(StopToken&& stopToken) noexcept
        : stopToken_((StopToken &&) stopToken) {}

    ~sync_wait_promise() {
      if (state_ == state::value) {
        value_.destruct();
      } else if (state_ == state::error) {
        exception_.destruct();
      }
    }

    receiver get_receiver() noexcept {
      return receiver{*this};
    }

    std::optional<T> get() && {
      switch (state_) {
        case state::done:
          return std::nullopt;
        case state::value:
          return std::move(value_).get();
        case state::error:
          std::rethrow_exception(exception_.get());
        default:
          assert(false);
          std::terminate();
      }
    }

   private:
    union {
      manual_lifetime<T> value_;
      manual_lifetime<std::exception_ptr> exception_;
    };

    state state_ = state::incomplete;
    StopToken stopToken_;
  };

 public:
  template <
      typename Sender,
      typename StopToken = unstoppable_token,
      typename Result = single_value_result_t<std::remove_cvref_t<Sender>>>
  std::optional<Result> sync_wait(Sender&& sender, StopToken st = {}) {
    using promise_t = sync_wait_promise<Result, StopToken&&>;
    promise_t promise{(StopToken &&) st};

    auto op = connect((Sender &&) sender, promise.get_receiver());
    start(op);

    run_until_empty();

    return std::move(promise).get();
  }

 private:
  void run_until_empty() noexcept;

  operation_base* head_ = nullptr;
};

} // namespace unifex
