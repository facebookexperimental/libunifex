/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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
#include <unifex/optional.hpp>

#include <chrono>
#include <exception>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
class thread_unsafe_event_loop;

namespace _thread_unsafe_event_loop {
  using clock_t = std::chrono::steady_clock;
  using time_point_t = clock_t::time_point;

  class cancel_callback;

  class operation_base {
    friend cancel_callback;
   protected:
    using execute_fn = void(operation_base*) noexcept;

    operation_base(thread_unsafe_event_loop& loop, execute_fn* execute) noexcept
      : loop_(loop), execute_(execute) {}

    operation_base(const operation_base&) = delete;
    operation_base(operation_base&&) = delete;

   public:
    void start() noexcept;

   private:
    friend thread_unsafe_event_loop;

    void execute() noexcept {
      this->execute_(this);
    }

    thread_unsafe_event_loop& loop_;
    operation_base* next_;
    operation_base** prevPtr_;
    execute_fn* execute_;

   protected:
    time_point_t dueTime_;
  };

  class cancel_callback {
   public:
    explicit cancel_callback(operation_base& op) noexcept
      : op_(&op) {}

    void operator()() noexcept;

   private:
    operation_base* const op_;
  };

  class scheduler;

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
  using after_operation = typename _after_op<Duration, remove_cvref_t<Receiver>>::type;

  template <typename Duration, typename Receiver>
  class _after_op<Duration, Receiver>::type final : public operation_base {
    friend schedule_after_sender<Duration>;
   public:
    void start() noexcept {
      this->dueTime_ = clock_t::now() + duration_;
      callback_.construct(
          get_stop_token(receiver_), cancel_callback{*this});
      operation_base::start();
    }

   private:
    template <typename Receiver2>
    explicit type(
        Receiver2&& r,
        Duration d,
        thread_unsafe_event_loop& loop)
        : operation_base(loop, &type::execute_impl)
        , receiver_((Receiver2 &&) r)
        , duration_(d) {}

    static void execute_impl(operation_base* p) noexcept {
      auto& self = *static_cast<type*>(p);
      self.callback_.destruct();
      if constexpr (is_stop_never_possible_v<
                        stop_token_type_t<Receiver&>>) {
        unifex::set_value(std::move(self.receiver_));
      } else {
        if (get_stop_token(self.receiver_).stop_requested()) {
          unifex::set_done(std::move(self.receiver_));
        } else {
          unifex::set_value(std::move(self.receiver_));
        }
      }
    }

    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    UNIFEX_NO_UNIQUE_ADDRESS Duration duration_;
    UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
        Receiver&>::template callback_type<cancel_callback>>
        callback_;
  };

  template <typename Duration>
  class _schedule_after_sender<Duration>::type {
    using schedule_after_sender = type;
   public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    static constexpr bool sends_done = true;

    template <typename Receiver>
    after_operation<Duration, remove_cvref_t<Receiver>> connect(Receiver&& r) const& {
      return after_operation<Duration, remove_cvref_t<Receiver>>{
          (Receiver &&) r, duration_, loop_};
    }
   private:
    friend scheduler;

    explicit type(
        thread_unsafe_event_loop& loop,
        Duration duration) noexcept
        : loop_(loop), duration_(duration) {}

    thread_unsafe_event_loop& loop_;
    Duration duration_;
  };

  struct schedule_at_sender;

  template <typename Receiver>
  struct _at_op {
    class type;
  };
  template <typename Receiver>
  using at_operation = typename _at_op<remove_cvref_t<Receiver>>::type;

  template <typename Receiver>
  class _at_op<Receiver>::type final : public operation_base {
   public:
    void start() noexcept {
      callback_.construct(
          get_stop_token(receiver_), cancel_callback{*this});
      operation_base::start();
    }

   private:
    friend schedule_at_sender;

    template <typename Receiver2>
    explicit type(
        Receiver2&& r,
        time_point_t tp,
        thread_unsafe_event_loop& loop)
        : operation_base(loop, &type::execute_impl), receiver_((Receiver2 &&) r) {
      this->dueTime_ = tp;
    }

    static void execute_impl(operation_base* p) noexcept {
      auto& self = *static_cast<type*>(p);
      self.callback_.destruct();
      if constexpr (is_stop_never_possible_v<
                        stop_token_type_t<Receiver&>>) {
        unifex::set_value(std::move(self.receiver_));
      } else {
        if (get_stop_token(self.receiver_).stop_requested()) {
          unifex::set_done(std::move(self.receiver_));
        } else {
          unifex::set_value(std::move(self.receiver_));
        }
      }
    }

    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
        Receiver&>::template callback_type<cancel_callback>>
        callback_;
  };

  struct schedule_at_sender {
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    static constexpr bool sends_done = true;

    template <typename Receiver>
    at_operation<remove_cvref_t<Receiver>> connect(Receiver&& r) const& {
      return at_operation<remove_cvref_t<Receiver>>{
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

  class scheduler {
   public:
    auto schedule_at(time_point_t dueTime) const noexcept {
      return schedule_at_sender{*loop_, dueTime};
    }

    template <typename Rep, typename Ratio>
    auto schedule_after(std::chrono::duration<Rep, Ratio> d) const noexcept {
      return schedule_after_sender<std::chrono::duration<Rep, Ratio>>{*loop_, d};
    }

    auto schedule() const noexcept {
      return schedule_after(std::chrono::milliseconds(0));
    }
    friend bool operator==(scheduler a, scheduler b) noexcept {
      return a.loop_ == b.loop_;
    }
    friend bool operator!=(scheduler a, scheduler b) noexcept {
      return a.loop_ != b.loop_;
    }

   private:
    friend thread_unsafe_event_loop;

    explicit scheduler(thread_unsafe_event_loop& loop) noexcept
      : loop_(&loop) {}

    thread_unsafe_event_loop* loop_;
  };

  template <typename T>
  struct _sync_wait_promise {
    class type;
  };
  template <typename T>
  using sync_wait_promise = typename _sync_wait_promise<T>::type;

  template <typename T>
  class _sync_wait_promise<T>::type {
    using sync_wait_promise = type;
    enum class state { incomplete, done, value, error };

    class receiver {
     public:
      template <typename... Values>
      void set_value(Values&&... values) && noexcept {
        UNIFEX_TRY {
          unifex::activate_union_member(promise_.value_, (Values &&) values...);
          promise_.state_ = state::value;
        } UNIFEX_CATCH (...) {
          unifex::activate_union_member(promise_.exception_, std::current_exception());
          promise_.state_ = state::error;
        }
      }

      void set_error(std::exception_ptr ex) && noexcept {
        unifex::activate_union_member(promise_.exception_, std::move(ex));
        promise_.state_ = state::error;
      }

      void set_done() && noexcept {
        promise_.state_ = state::done;
      }

     private:
      friend sync_wait_promise;

      explicit receiver(sync_wait_promise& promise) noexcept
        : promise_(promise) {}

      sync_wait_promise& promise_;
    };

   public:
    type() noexcept {}

    ~type() {
      if (state_ == state::value) {
        unifex::deactivate_union_member(value_);
      } else if (state_ == state::error) {
        unifex::deactivate_union_member(exception_);
      }
    }

    receiver get_receiver() noexcept {
      return receiver{*this};
    }

    optional<T> get() && {
      switch (state_) {
        case state::done:
          return nullopt;
        case state::value:
          return std::move(value_).get();
        case state::error:
          std::rethrow_exception(exception_.get());
        default:
          UNIFEX_ASSERT(false);
          std::terminate();
      }
    }

   private:
    union {
      manual_lifetime<T> value_;
      manual_lifetime<std::exception_ptr> exception_;
    };

    state state_ = state::incomplete;
  };
} // namespace _thread_unsafe_event_loop

class thread_unsafe_event_loop {
  using operation_base = _thread_unsafe_event_loop::operation_base;
  using scheduler = _thread_unsafe_event_loop::scheduler;
  using cancel_callback = _thread_unsafe_event_loop::cancel_callback;

  friend operation_base;
  friend cancel_callback;

  void enqueue(operation_base* op) noexcept;
  void run_until_empty() noexcept;

  operation_base* head_ = nullptr;
 public:
  using clock_t = _thread_unsafe_event_loop::clock_t;
  using time_point_t = _thread_unsafe_event_loop::time_point_t;

  scheduler get_scheduler() noexcept {
    return scheduler{*this};
  }

  template <
      typename Sender,
      typename Result = sender_single_value_result_t<remove_cvref_t<Sender>>>
  optional<Result> sync_wait(Sender&& sender) {
    using promise_t = _thread_unsafe_event_loop::sync_wait_promise<Result>;
    promise_t promise;

    auto op = connect((Sender &&) sender, promise.get_receiver());
    start(op);

    run_until_empty();

    return std::move(promise).get();
  }
};

namespace _thread_unsafe_event_loop {
  inline void operation_base::start() noexcept {
    loop_.enqueue(this);
  }
}

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
