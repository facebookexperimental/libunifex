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
#if !UNIFEX_NO_EPOLL

#include <unifex/detail/atomic_intrusive_queue.hpp>
#include <unifex/detail/intrusive_heap.hpp>
#include <unifex/detail/intrusive_queue.hpp>
#include <unifex/pipe_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/span.hpp>
#include <unifex/stop_token_concepts.hpp>

#include <unifex/linux/monotonic_clock.hpp>
#include <unifex/linux/safe_file_descriptor.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <system_error>
#include <utility>

#include <sys/uio.h>
#include <sys/epoll.h>

namespace unifex {
namespace linuxos {

class io_epoll_context {
 public:
  class schedule_sender;
  class schedule_at_sender;
  template <typename Duration>
  class schedule_after_sender;
  class scheduler;
  class read_sender;
  class write_sender;
  class async_reader;
  class async_writer;

  io_epoll_context();

  ~io_epoll_context();

  template <typename StopToken>
  void run(StopToken stopToken);

  scheduler get_scheduler() noexcept;

 private:
  struct operation_base {
    operation_base() noexcept {}
    operation_base* next_;
    void (*execute_)(operation_base*) noexcept;
  };

  struct completion_base : operation_base {
  };

  struct stop_operation : operation_base {
    stop_operation() noexcept {
      this->execute_ = [](operation_base * op) noexcept {
        static_cast<stop_operation*>(op)->shouldStop_ = true;
      };
    }
    bool shouldStop_ = false;
  };

  using time_point = linuxos::monotonic_clock::time_point;

  struct schedule_at_operation : operation_base {
    explicit schedule_at_operation(
        io_epoll_context& context,
        const time_point& dueTime,
        bool canBeCancelled) noexcept
        : context_(context),
          dueTime_(dueTime),
          canBeCancelled_(canBeCancelled) {}

    schedule_at_operation* timerNext_;
    schedule_at_operation* timerPrev_;
    io_epoll_context& context_;
    time_point dueTime_;
    bool canBeCancelled_;

    static constexpr std::uint32_t timer_elapsed_flag = 1;
    static constexpr std::uint32_t cancel_pending_flag = 2;
    std::atomic<std::uint32_t> state_ = 0;
  };

  using operation_queue =
      intrusive_queue<operation_base, &operation_base::next_>;

  using timer_heap = intrusive_heap<
      schedule_at_operation,
      &schedule_at_operation::timerNext_,
      &schedule_at_operation::timerPrev_,
      time_point,
      &schedule_at_operation::dueTime_>;

  bool is_running_on_io_thread() const noexcept;
  void run_impl(const bool& shouldStop);

  void schedule_impl(operation_base* op);
  void schedule_local(operation_base* op) noexcept;
  void schedule_local(operation_queue ops) noexcept;
  void schedule_remote(operation_base* op) noexcept;

  // Insert the timer operation into the queue of timers.
  // Must be called from the I/O thread.
  void schedule_at_impl(schedule_at_operation* op) noexcept;

  // Execute all ready-to-run items on the local queue.
  // Will not run other items that were enqueued during the execution of the
  // items that were already enqueued.
  // This bounds the amount of work to a finite amount.
  void execute_pending_local() noexcept;

  // Check if any completion queue items are available and if so add them
  // to the local queue.
  void acquire_completion_queue_items();

  // collect the contents of the remote queue and pass them to schedule_local
  //
  // Returns true if successful.
  //
  // Returns false if some other thread concurrently enqueued work to the remote queue.
  bool try_schedule_local_remote_queue_contents() noexcept;

  // Signal the remote queue eventfd.
  //
  // This should only be called after trying to enqueue() work
  // to the remoteQueue and being told that the I/O thread is
  // inactive.
  void signal_remote_queue();

  void remove_timer(schedule_at_operation* op) noexcept;
  void update_timers() noexcept;
  bool try_submit_timer_io(const time_point& dueTime) noexcept;

  void* timer_user_data() const {
    return const_cast<void*>(static_cast<const void*>(&timers_));
  }

  ////////
  // Data that does not change once initialised.

  // Resources
  safe_file_descriptor epollFd_;
  safe_file_descriptor timerFd_;
  safe_file_descriptor remoteQueueEventFd_;

  ///////////////////
  // Data that is modified by I/O thread

  // Local queue for operations that are ready to execute.
  operation_queue localQueue_;

  // Set of operations waiting to be executed at a specific time.
  timer_heap timers_;

  // The time that the current timer operation submitted to the kernel
  // is due to elapse.
  std::optional<time_point> currentDueTime_;

  bool remoteQueueReadSubmitted_ = false;
  bool timersAreDirty_ = false;

  //////////////////
  // Data that is modified by remote threads

  // Queue of operations enqueued by remote threads.
  atomic_intrusive_queue<operation_base, &operation_base::next_> remoteQueue_;
};

template <typename StopToken>
void io_epoll_context::run(StopToken stopToken) {
  stop_operation stopOp;
  auto onStopRequested = [&] { this->schedule_impl(&stopOp); };
  typename StopToken::template callback_type<decltype(onStopRequested)>
      stopCallback{std::move(stopToken), std::move(onStopRequested)};
  run_impl(stopOp.shouldStop_);
}

class io_epoll_context::schedule_sender {
  template <typename Receiver>
  class operation : private operation_base {
   public:
    void start() noexcept {
      try {
        context_.schedule_impl(this);
      } catch (...) {
        unifex::set_error(
            static_cast<Receiver&&>(receiver_), std::current_exception());
      }
    }

   private:
    friend schedule_sender;

    template <typename Receiver2>
    explicit operation(io_epoll_context& context, Receiver2&& r)
        : context_(context), receiver_((Receiver2 &&) r) {
      this->execute_ = &execute_impl;
    }

    static void execute_impl(operation_base* p) noexcept {
      operation& op = *static_cast<operation*>(p);
      if constexpr (!is_stop_never_possible_v<stop_token_type_t<Receiver>>) {
        if (get_stop_token(op.receiver_).stop_requested()) {
          unifex::set_done(static_cast<Receiver&&>(op.receiver_));
          return;
        }
      }
      if constexpr (is_nothrow_callable_v<unifex::tag_t<unifex::set_value>&, Receiver>) {
        unifex::set_value(static_cast<Receiver&&>(op.receiver_));
      } else {
        try {
          unifex::set_value(static_cast<Receiver&&>(op.receiver_));
        } catch (...) {
          unifex::set_error(
              static_cast<Receiver&&>(op.receiver_), std::current_exception());
        }
      }
    }

    io_epoll_context& context_;
    Receiver receiver_;
  };

 public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  template <typename Receiver>
  operation<std::remove_reference_t<Receiver>> connect(Receiver&& r) {
    return operation<std::remove_reference_t<Receiver>>{context_,
                                                        (Receiver &&) r};
  }

 private:
  friend io_epoll_context::scheduler;

  explicit schedule_sender(io_epoll_context& context) noexcept
      : context_(context) {}

  io_epoll_context& context_;
};

class io_epoll_context::schedule_at_sender {
  template <typename Receiver>
  struct operation : schedule_at_operation {
    static constexpr bool is_stop_ever_possible =
        !is_stop_never_possible_v<stop_token_type_t<Receiver>>;

   public:
    explicit operation(
        io_epoll_context& context,
        const time_point& dueTime,
        Receiver&& r)
        : schedule_at_operation(
              context,
              dueTime,
              get_stop_token(r).stop_possible()),
          receiver_((Receiver &&) r) {}

    void start() noexcept {
      if (this->context_.is_running_on_io_thread()) {
        start_local();
      } else {
        start_remote();
      }
    }

   private:
    static void on_schedule_complete(operation_base* op) noexcept {
      static_cast<operation*>(op)->start_local();
    }

    static void complete_with_done(operation_base* op) noexcept {
      // Avoid instantiating set_done() if we're not going to call it.
      if constexpr (is_stop_ever_possible) {
        auto& timerOp = *static_cast<operation*>(op);
        unifex::set_done(std::move(timerOp).receiver_);
      } else {
        // This should never be called if stop is not possible.
        assert(false);
      }
    }

    // Executed when the timer gets to the front of the ready-to-run queue.
    static void maybe_complete_with_value(operation_base* op) noexcept {
      auto& timerOp = *static_cast<operation*>(op);
      if constexpr (is_stop_ever_possible) {
        timerOp.stopCallback_.destruct();

        if (get_stop_token(timerOp.receiver_).stop_requested()) {
          complete_with_done(op);
          return;
        }
      }

      unifex::set_value(std::move(timerOp).receiver_);
    }

    static void remove_timer_from_queue_and_complete_with_done(
        operation_base* op) noexcept {
      // Avoid instantiating set_done() if we're never going to call it.
      if constexpr (is_stop_ever_possible) {
        auto& timerOp = *static_cast<operation*>(op);
        assert(timerOp.context_.is_running_on_io_thread());

        timerOp.stopCallback_.destruct();

        auto state = timerOp.state_.load(std::memory_order_relaxed);
        if ((state & schedule_at_operation::timer_elapsed_flag) == 0) {
          // Timer not yet removed from the timers_ list. Do that now.
          timerOp.context_.remove_timer(&timerOp);
        }

        unifex::set_done(std::move(timerOp).receiver_);
      } else {
        // Should never be called if stop is not possible.
        assert(false);
      }
    }

    void start_local() noexcept {
      if constexpr (is_stop_ever_possible) {
        if (get_stop_token(receiver_).stop_requested()) {
          // Stop already requested. Don't bother adding the timer.
          this->execute_ = &operation::complete_with_done;
          this->context_.schedule_local(this);
          return;
        }
      }

      this->execute_ = &operation::maybe_complete_with_value;
      this->context_.schedule_at_impl(this);

      if constexpr (is_stop_ever_possible) {
        stopCallback_.construct(
            get_stop_token(receiver_), cancel_callback{*this});
      }
    }

    void start_remote() noexcept {
      this->execute_ = &operation::on_schedule_complete;
      this->context_.schedule_remote(this);
    }

    void request_stop() noexcept {
      if (context_.is_running_on_io_thread()) {
        request_stop_local();
      } else {
        request_stop_remote();
      }
    }

    void request_stop_local() noexcept {
      assert(context_.is_running_on_io_thread());

      stopCallback_.destruct();

      this->execute_ = &operation::complete_with_done;

      auto state = this->state_.load(std::memory_order_relaxed);
      if ((state & schedule_at_operation::timer_elapsed_flag) == 0) {
        // Timer not yet elapsed.
        // Remove timer from list of timers and enqueue cancellation.
        context_.remove_timer(this);
        context_.schedule_local(this);
      } else {
        // Timer already elapsed and added to ready-to-run queue.
      }
    }

    void request_stop_remote() noexcept {
      auto oldState = this->state_.fetch_add(
          schedule_at_operation::cancel_pending_flag,
          std::memory_order_acq_rel);
      if ((oldState & schedule_at_operation::timer_elapsed_flag) == 0) {
        // Timer had not yet elapsed.
        // We are responsible for scheduling the completion of this timer
        // operation.
        this->execute_ =
            &operation::remove_timer_from_queue_and_complete_with_done;
        this->context_.schedule_remote(this);
      }
    }

    struct cancel_callback {
      operation& op_;

      void operator()() noexcept {
        op_.request_stop();
      }
    };

    Receiver receiver_;
    manual_lifetime<typename stop_token_type_t<
        Receiver>::template callback_type<cancel_callback>>
        stopCallback_;
  };

 public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  explicit schedule_at_sender(
      io_epoll_context& context,
      const time_point& dueTime) noexcept
      : context_(context), dueTime_(dueTime) {}

  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& r) && {
    return operation<std::remove_cvref_t<Receiver>>{
        context_, dueTime_, (Receiver &&) r};
  }
  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& r) & {
    return operation<std::remove_cvref_t<Receiver>>{
        context_, dueTime_, (Receiver &&) r};
  }
  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& r) const & {
    return operation<std::remove_cvref_t<Receiver>>{
        context_, dueTime_, (Receiver &&) r};
  }

 private:
  io_epoll_context& context_;
  time_point dueTime_;
};

class io_epoll_context::scheduler {
 public:
  scheduler(const scheduler&) noexcept = default;
  scheduler& operator=(const scheduler&) = default;
  ~scheduler() = default;

  schedule_sender schedule() const noexcept {
    return schedule_sender{*context_};
  }

  time_point now() const noexcept {
    return monotonic_clock::now();
  }

  schedule_at_sender schedule_at(const time_point& dueTime) const noexcept {
    return schedule_at_sender{*context_, dueTime};
  }

 private:
  friend io_epoll_context;

  friend std::pair<async_reader, async_writer> tag_invoke(
      tag_t<open_pipe>,
      scheduler s);

  friend bool operator==(const scheduler& a, const scheduler& b) noexcept {
    return a.context_ == b.context_;
  }

  explicit scheduler(io_epoll_context& context) noexcept : context_(&context) {}

  io_epoll_context* context_;
};

inline io_epoll_context::scheduler io_epoll_context::get_scheduler() noexcept {
  return scheduler{*this};
}

class io_epoll_context::read_sender {

  template <typename Receiver>
  class operation : private completion_base {
    friend io_epoll_context;

    static constexpr bool is_stop_ever_possible =
        !is_stop_never_possible_v<stop_token_type_t<Receiver>>;
   public:
    template <typename Receiver2>
    explicit operation(const read_sender& sender, Receiver2&& r)
        : context_(sender.context_),
          fd_(sender.fd_),
          receiver_((Receiver2 &&) r) {
      buffer_[0].iov_base = sender.buffer_.data();
      buffer_[0].iov_len = sender.buffer_.size();

    }

    void start() noexcept {
      if (!context_.is_running_on_io_thread()) {
        this->execute_ = &operation::on_schedule_complete;
        context_.schedule_remote(this);
      } else {
        start_io();
      }
    }

   private:
    static void on_schedule_complete(operation_base* op) noexcept {
      auto& self = *static_cast<operation*>(op);
      self.execute_ = nullptr;
      self.start_io();
    }

    void start_io() noexcept {
      assert(context_.is_running_on_io_thread());

      auto result = readv(fd_, buffer_, 1);
      if (result == -EAGAIN || result == -EWOULDBLOCK || result == -EPERM) {
        if constexpr (is_stop_ever_possible) {
          stopCallback_.construct(
              get_stop_token(receiver_), cancel_callback{*this});
        }

        this->execute_ = &operation::on_read_complete;
        epoll_event event;
        event.data.ptr = this;
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
        (void)epoll_ctl(context_.epollFd_.get(), EPOLL_CTL_ADD, fd_, &event);
      } else if (result == -ECANCELED) {
        unifex::set_done(std::move(receiver_));
      } else if (result >= 0) {
        unifex::set_value(std::move(receiver_), ssize_t(result));
      } else {
        printf("start_io::readv failed\n");
        unifex::set_error(
            std::move(receiver_),
            std::error_code{-int(result), std::system_category()});
      }
    }

    static void on_read_complete(operation_base* op) noexcept {
      auto& self = *static_cast<operation*>(op);

      self.stopCallback_.destruct();

      epoll_event event = {};
      (void)epoll_ctl(self.context_.epollFd_.get(), EPOLL_CTL_DEL, self.fd_, &event);
      self.execute_ = nullptr;

      auto result = readv(self.fd_, self.buffer_, 1);
      assert(result != -EAGAIN);
      assert(result != -EWOULDBLOCK);
      if (result == -ECANCELED) {
        unifex::set_done(std::move(self.receiver_));
      } else if (result >= 0) {
        unifex::set_value(std::move(self.receiver_), ssize_t(result));
      } else {
        printf("on_read_complete::readv failed\n");
        unifex::set_error(
            std::move(self.receiver_),
            std::error_code{-int(result), std::system_category()});
      }
    }

    static void complete_with_done(operation_base* op) noexcept {
      auto& self = *static_cast<operation*>(op);
      self.execute_ = nullptr;
      self.request_stop_local();
    }

    void request_stop() noexcept {
      if (context_.is_running_on_io_thread()) {
        request_stop_local();
      } else {
        request_stop_remote();
      }
    }

    void request_stop_local() noexcept {
      assert(context_.is_running_on_io_thread());

      stopCallback_.destruct();

      epoll_event event = {};
      (void)epoll_ctl(context_.epollFd_.get(), EPOLL_CTL_DEL, fd_, &event);
      execute_ = nullptr;

      // Avoid instantiating set_done() if we're not going to call it.
      if constexpr (is_stop_ever_possible) {
        unifex::set_done(std::move(receiver_));
      } else {
        // This should never be called if stop is not possible.
        assert(false);
      }
    }

    void request_stop_remote() noexcept {
      epoll_event event = {};
      (void)epoll_ctl(context_.epollFd_.get(), EPOLL_CTL_DEL, fd_, &event);

      this->execute_ = &operation::complete_with_done;
      this->context_.schedule_remote(this);
    }

    struct cancel_callback {
      operation& op_;

      void operator()() noexcept {
        op_.request_stop();
      }
    };

    io_epoll_context& context_;
    int fd_;
    iovec buffer_[1];
    Receiver receiver_;
    manual_lifetime<typename stop_token_type_t<
      Receiver>::template callback_type<cancel_callback>>
      stopCallback_;
  };

 public:
  // Produces number of bytes read.
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<ssize_t>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::error_code>;

  explicit read_sender(
      io_epoll_context& context,
      int fd,
      span<std::byte> buffer) noexcept
      : context_(context), fd_(fd), buffer_(buffer) {}

  template <typename Receiver>
  operation<std::decay_t<Receiver>> connect(Receiver&& r) && {
    return operation<std::decay_t<Receiver>>{*this, (Receiver &&) r};
  }
  template <typename Receiver>
  operation<std::decay_t<Receiver>> connect(Receiver&& r) & {
    return operation<std::decay_t<Receiver>>{*this, (Receiver &&) r};
  }
  template <typename Receiver>
  operation<std::decay_t<Receiver>> connect(Receiver&& r) const & {
    return operation<std::decay_t<Receiver>>{*this, (Receiver &&) r};
  }

 private:
  io_epoll_context& context_;
  int fd_;
  span<std::byte> buffer_;
};

class io_epoll_context::write_sender {

  template <typename Receiver>
  class operation : private completion_base {
    friend io_epoll_context;

    static constexpr bool is_stop_ever_possible =
        !is_stop_never_possible_v<stop_token_type_t<Receiver>>;
   public:
    template <typename Receiver2>
    explicit operation(const write_sender& sender, Receiver2&& r)
        : context_(sender.context_),
          fd_(sender.fd_),
          receiver_((Receiver2 &&) r) {
      buffer_[0].iov_base = (void*)sender.buffer_.data();
      buffer_[0].iov_len = sender.buffer_.size();
    }

    void start() noexcept {
      if (!context_.is_running_on_io_thread()) {
        this->execute_ = &operation::on_schedule_complete;
        context_.schedule_remote(this);
      } else {
        start_io();
      }
    }

   private:

    static void on_schedule_complete(operation_base* op) noexcept {
      auto& self = *static_cast<operation*>(op);
      self.execute_ = nullptr;
      self.start_io();
    }

    void start_io() noexcept {
      assert(context_.is_running_on_io_thread());

      auto result = writev(fd_, buffer_, 1);
      if (result == -EAGAIN || result == -EWOULDBLOCK || result == -EPERM) {
        if constexpr (is_stop_ever_possible) {
          stopCallback_.construct(
              get_stop_token(receiver_), cancel_callback{*this});
        }

        this->execute_ = &operation::on_write_complete;
        epoll_event event;
        event.data.ptr = this;
        event.events = EPOLLOUT | EPOLLRDHUP | EPOLLHUP;
        (void)epoll_ctl(context_.epollFd_.get(), EPOLL_CTL_ADD, fd_, &event);
      } else if (result == -ECANCELED) {
        unifex::set_done(std::move(receiver_));
      } else if (result >= 0) {
        unifex::set_value(std::move(receiver_), ssize_t(result));
      } else {
        unifex::set_error(
            std::move(receiver_),
            std::error_code{-int(result), std::system_category()});
      }
    }

    static void on_write_complete(operation_base* op) noexcept {
      auto& self = *static_cast<operation*>(op);

      self.stopCallback_.destruct();

      epoll_event event = {};
      (void)epoll_ctl(self.context_.epollFd_.get(), EPOLL_CTL_DEL, self.fd_, &event);
      self.execute_ = nullptr;

      auto result = writev(self.fd_, self.buffer_, 1);
      assert(result != -EAGAIN);
      assert(result != -EWOULDBLOCK);
      if (result == -ECANCELED) {
        unifex::set_done(std::move(self.receiver_));
      } else if (result >= 0) {
        unifex::set_value(std::move(self.receiver_), ssize_t(result));
      } else {
        unifex::set_error(
            std::move(self.receiver_),
            std::error_code{-int(result), std::system_category()});
      }
    }

    static void complete_with_done(operation_base* op) noexcept {
      auto& self = *static_cast<operation*>(op);
      self.execute_ = nullptr;
      self.request_stop_local();
    }

    void request_stop() noexcept {
      if (context_.is_running_on_io_thread()) {
        request_stop_local();
      } else {
        request_stop_remote();
      }
    }

    void request_stop_local() noexcept {
      assert(context_.is_running_on_io_thread());

      stopCallback_.destruct();

      epoll_event event = {};
      (void)epoll_ctl(context_.epollFd_.get(), EPOLL_CTL_DEL, fd_, &event);
      execute_ = nullptr;

      // Avoid instantiating set_done() if we're not going to call it.
      if constexpr (is_stop_ever_possible) {
        unifex::set_done(std::move(receiver_));
      } else {
        // This should never be called if stop is not possible.
        assert(false);
      }
    }

    void request_stop_remote() noexcept {
      epoll_event event = {};
      (void)epoll_ctl(context_.epollFd_.get(), EPOLL_CTL_DEL, fd_, &event);

      this->execute_ = &operation::complete_with_done;
      this->context_.schedule_remote(this);
    }

    struct cancel_callback {
      operation& op_;

      void operator()() noexcept {
        op_.request_stop();
      }
    };

    io_epoll_context& context_;
    int fd_;
    iovec buffer_[1];
    Receiver receiver_;
    manual_lifetime<typename stop_token_type_t<
      Receiver>::template callback_type<cancel_callback>>
      stopCallback_;
  };

 public:
  // Produces number of bytes read.
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<ssize_t>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::error_code>;

  explicit write_sender(
      io_epoll_context& context,
      int fd,
      span<const std::byte> buffer) noexcept
      : context_(context), fd_(fd), buffer_(buffer) {}

  template <typename Receiver>
  operation<std::decay_t<Receiver>> connect(Receiver&& r) && {
    return operation<std::decay_t<Receiver>>{*this, (Receiver &&) r};
  }
  template <typename Receiver>
  operation<std::decay_t<Receiver>> connect(Receiver&& r) & {
    return operation<std::decay_t<Receiver>>{*this, (Receiver &&) r};
  }
  template <typename Receiver>
  operation<std::decay_t<Receiver>> connect(Receiver&& r) const & {
    return operation<std::decay_t<Receiver>>{*this, (Receiver &&) r};
  }

 private:
  io_epoll_context& context_;
  int fd_;
  span<const std::byte> buffer_;
};

class io_epoll_context::async_reader {
 public:

  explicit async_reader(io_epoll_context& context, int fd) noexcept
      : context_(context), fd_(fd) {}

 private:
  friend scheduler;

  friend read_sender tag_invoke(
      tag_t<async_read_some>,
      async_reader& reader,
      span<std::byte> buffer) noexcept {
    return read_sender{reader.context_, reader.fd_.get(), buffer};
  }

  io_epoll_context& context_;
  safe_file_descriptor fd_;
};

class io_epoll_context::async_writer {
 public:

  explicit async_writer(io_epoll_context& context, int fd) noexcept
      : context_(context), fd_(fd) {}

 private:
  friend scheduler;

  friend write_sender tag_invoke(
      tag_t<async_write_some>,
      async_writer& writer,
      span<const std::byte> buffer) noexcept {
    return write_sender{writer.context_, writer.fd_.get(), buffer};
  }

  io_epoll_context& context_;
  safe_file_descriptor fd_;
};

} // namespace linuxos
} // namespace unifex

#endif // !UNIFEX_NO_EPOLL
