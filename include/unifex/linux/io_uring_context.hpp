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

#include <unifex/config.hpp>
#if !UNIFEX_NO_LIBURING

#include <unifex/detail/atomic_intrusive_queue.hpp>
#include <unifex/detail/intrusive_heap.hpp>
#include <unifex/detail/intrusive_queue.hpp>
#include <unifex/file_concepts.hpp>
#include <unifex/filesystem.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/span.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/optional.hpp>

#include <unifex/linux/mmap_region.hpp>
#include <unifex/linux/monotonic_clock.hpp>
#include <unifex/linux/safe_file_descriptor.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <utility>

#include <liburing/io_uring.h>

#include <sys/uio.h>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace linuxos {

class io_uring_context {
 public:
  class schedule_sender;
  class schedule_at_sender;
  template <typename Duration>
  class schedule_after_sender;
  class read_sender;
  class write_sender;
  class async_read_only_file;
  class async_read_write_file;
  class async_write_only_file;
  class scheduler;

  io_uring_context();

  ~io_uring_context();

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
    int result_;
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
        io_uring_context& context,
        const time_point& dueTime,
        bool canBeCancelled) noexcept
        : context_(context),
          dueTime_(dueTime),
          canBeCancelled_(canBeCancelled) {}

    schedule_at_operation* timerNext_;
    schedule_at_operation* timerPrev_;
    io_uring_context& context_;
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

  // Schedule some operation to be run when there is next available I/O slots.
  void schedule_pending_io(operation_base* op) noexcept;
  void reschedule_pending_io(operation_base* op) noexcept;

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
  void acquire_completion_queue_items() noexcept;

  // Check if any completion queue items have been enqueued and move them
  // to the local queue.
  void acquire_remote_queued_items() noexcept;

  // Submit a request to the submission queue containing an IORING_OP_POLL_ADD
  // for the remote queue eventfd as a way of registering for asynchronous
  // notification of someone enqueueing
  //
  // Returns true if successful. If so then it is no longer permitted
  // to call 'acquire_remote_queued_items()' until after the completion
  // for this POLL_ADD operation is received.
  //
  // Returns false if either no more operations can be submitted at this
  // time (submission queue full or too many pending completions) or if
  // some other thread concurrently enqueued work to the remote queue.
  bool try_register_remote_queue_notification() noexcept;

  // Signal the remote queue eventfd.
  //
  // This should only be called after trying to enqueue() work
  // to the remoteQueue and being told that the I/O thread is
  // inactive.
  void signal_remote_queue();

  void remove_timer(schedule_at_operation* op) noexcept;
  void update_timers() noexcept;
  bool try_submit_timer_io(const time_point& dueTime) noexcept;
  bool try_submit_timer_io_cancel() noexcept;

  // Try to submit an entry to the submission queue
  //
  // If there is space in the queue then populateSqe
  template <typename PopulateFn>
  bool try_submit_io(PopulateFn populateSqe) noexcept;

  // Total number of operations submitted that have not yet
  // completed.
  std::uint32_t pending_operation_count() const noexcept {
    return cqPendingCount_ + sqUnflushedCount_;
  }

  // Query whether there is space in the submission ring buffer
  // and space in the completion ring buffer for an additional
  // entry.
  bool can_submit_io() const noexcept {
    return sqUnflushedCount_ < sqEntryCount_ &&
        pending_operation_count() < cqEntryCount_;
  }

  std::uintptr_t timer_user_data() const {
    return reinterpret_cast<std::uintptr_t>(&timers_);
  }

  std::uintptr_t remove_timer_user_data() const {
    return reinterpret_cast<std::uintptr_t>(&currentDueTime_);
  }

  struct __kernel_timespec {
    int64_t tv_sec;
    long long tv_nsec;
  };

  ////////
  // Data that does not change once initialised.

  // Submission queue state
  std::uint32_t sqEntryCount_;
  std::uint32_t sqMask_;
  io_uring_sqe* sqEntries_;
  unsigned* sqIndexArray_;
  const std::atomic<unsigned>* sqHead_;
  std::atomic<unsigned>* sqTail_;
  std::atomic<unsigned>* sqFlags_;
  std::atomic<unsigned>* sqDropped_;

  // Completion queue state
  std::uint32_t cqEntryCount_;
  std::uint32_t cqMask_;
  io_uring_cqe* cqEntries_;
  std::atomic<unsigned>* cqHead_;
  const std::atomic<unsigned>* cqTail_;
  const std::atomic<unsigned>* cqOverflow_;

  // Resources
  safe_file_descriptor iouringFd_;
  safe_file_descriptor remoteQueueEventFd_;
  mmap_region cqMmap_;
  mmap_region sqMmap_;
  mmap_region sqeMmap_;

  ///////////////////
  // Data that is modified by I/O thread

  // Local queue for operations that are ready to execute.
  operation_queue localQueue_;

  // Operations that are waiting for more space in the I/O queues.
  operation_queue pendingIoQueue_;

  // Set of operations waiting to be executed at a specific time.
  timer_heap timers_;

  // The time that the current timer operation submitted to the kernel
  // is due to elapse.
  optional<time_point> currentDueTime_;

  // Number of unflushed I/O submission entries.
  std::uint32_t sqUnflushedCount_ = 0;

  // Number of submitted operations that have not yet received a completion.
  // We should ensure this number is never greater than cqEntryCount_ so that
  // we don't end up with an overflowed completion queue.
  std::uint32_t cqPendingCount_ = 0;

  bool remoteQueueReadSubmitted_ = false;
  bool timersAreDirty_ = false;

  std::uint32_t activeTimerCount_ = 0;

  __kernel_timespec time_;

  //////////////////
  // Data that is modified by remote threads

  // Queue of operations enqueued by remote threads.
  atomic_intrusive_queue<operation_base, &operation_base::next_> remoteQueue_;
};

template <typename StopToken>
void io_uring_context::run(StopToken stopToken) {
  stop_operation stopOp;
  auto onStopRequested = [&] { this->schedule_impl(&stopOp); };
  typename StopToken::template callback_type<decltype(onStopRequested)>
      stopCallback{std::move(stopToken), std::move(onStopRequested)};
  run_impl(stopOp.shouldStop_);
}

template <typename PopulateFn>
bool io_uring_context::try_submit_io(PopulateFn populateSqe) noexcept {
  UNIFEX_ASSERT(is_running_on_io_thread());

  if (pending_operation_count() < cqEntryCount_) {
    // Haven't reached limit of completion-queue yet.
    const auto tail = sqTail_->load(std::memory_order_relaxed);
    const auto head = sqHead_->load(std::memory_order_acquire);
    const auto usedCount = (tail - head);
    UNIFEX_ASSERT(usedCount <= sqEntryCount_);
    if (usedCount < sqEntryCount_) {
      // There is space in the submission-queue.
      const auto index = tail & sqMask_;
      auto& sqe = sqEntries_[index];

      static_assert(noexcept(populateSqe(sqe)));

      // nullify the struct
      std::memset(&sqe, 0, sizeof(sqe));

      if constexpr (std::is_void_v<decltype(populateSqe(sqe))>) {
        populateSqe(sqe);
      } else {
        if (!populateSqe(sqe)) {
          return false;
        }
      }

      sqIndexArray_[index] = index;
      sqTail_->store(tail + 1, std::memory_order_release);
      ++sqUnflushedCount_;
      return true;
    }
  }

  return false;
}

class io_uring_context::schedule_sender {
  template <typename Receiver>
  class operation : private operation_base {
   public:
    void start() noexcept {
      UNIFEX_TRY {
        context_.schedule_impl(this);
      } UNIFEX_CATCH (...) {
        unifex::set_error(
            static_cast<Receiver&&>(receiver_), std::current_exception());
      }
    }

   private:
    friend schedule_sender;

    template <typename Receiver2>
    explicit operation(io_uring_context& context, Receiver2&& r)
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

      if constexpr (noexcept(unifex::set_value(static_cast<Receiver&&>(op.receiver_)))) {
        unifex::set_value(static_cast<Receiver&&>(op.receiver_));
      } else {
        UNIFEX_TRY {
          unifex::set_value(static_cast<Receiver&&>(op.receiver_));
        } UNIFEX_CATCH (...) {
          unifex::set_error(static_cast<Receiver&&>(op.receiver_), std::current_exception());
        }
      }
    }

    io_uring_context& context_;
    Receiver receiver_;
  };

 public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  template <typename Receiver>
  operation<std::remove_reference_t<Receiver>> connect(Receiver&& r) {
    return operation<std::remove_reference_t<Receiver>>{context_,
                                                        (Receiver &&) r};
  }

 private:
  friend io_uring_context::scheduler;

  explicit schedule_sender(io_uring_context& context) noexcept
      : context_(context) {}

  io_uring_context& context_;
};

class io_uring_context::read_sender {
  using offset_t = std::int64_t;

  template <typename Receiver>
  class operation : private completion_base {
    friend io_uring_context;

   public:
    template <typename Receiver2>
    explicit operation(const read_sender& sender, Receiver2&& r)
        : context_(sender.context_),
          fd_(sender.fd_),
          offset_(sender.offset_),
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
      static_cast<operation*>(op)->start_io();
    }

    void start_io() noexcept {
      UNIFEX_ASSERT(context_.is_running_on_io_thread());

      auto populateSqe = [this](io_uring_sqe & sqe) noexcept {
        sqe.opcode = IORING_OP_READV;
        sqe.fd = fd_;
        sqe.off = offset_;
        sqe.addr = reinterpret_cast<std::uintptr_t>(&buffer_[0]);
        sqe.len = 1;
        sqe.user_data = reinterpret_cast<std::uintptr_t>(
            static_cast<completion_base*>(this));

        this->execute_ = &operation::on_read_complete;
      };

      if (!context_.try_submit_io(populateSqe)) {
        this->execute_ = &operation::on_schedule_complete;
        context_.schedule_pending_io(this);
      }
    }

    static void on_read_complete(operation_base* op) noexcept {
      auto& self = *static_cast<operation*>(op);
      if (self.result_ >= 0) {
        if constexpr (noexcept(unifex::set_value(std::move(self.receiver_), ssize_t(self.result_)))) {
          unifex::set_value(std::move(self.receiver_), ssize_t(self.result_));
        } else {
          UNIFEX_TRY {
            unifex::set_value(std::move(self.receiver_), ssize_t(self.result_));
          } UNIFEX_CATCH (...) {
            unifex::set_error(std::move(self.receiver_), std::current_exception());
          }
        }
      } else if (self.result_ == -ECANCELED) {
        unifex::set_done(std::move(self.receiver_));
      } else {
        unifex::set_error(
            std::move(self.receiver_),
            std::error_code{-self.result_, std::system_category()});
      }
    }

    io_uring_context& context_;
    int fd_;
    offset_t offset_;
    iovec buffer_[1];
    Receiver receiver_;
  };

 public:
  // Produces number of bytes read.
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<ssize_t>>;

  // Note: Only case it might complete with exception_ptr is if the
  // receiver's set_value() exits with an exception.
  template <template <typename...> class Variant>
  using error_types = Variant<std::error_code, std::exception_ptr>;

  static constexpr bool sends_done = true;

  explicit read_sender(
      io_uring_context& context,
      int fd,
      offset_t offset,
      span<std::byte> buffer) noexcept
      : context_(context), fd_(fd), offset_(offset), buffer_(buffer) {}

  template <typename Receiver>
  operation<remove_cvref_t<Receiver>> connect(Receiver&& r) && {
    return operation<remove_cvref_t<Receiver>>{*this, (Receiver &&) r};
  }

 private:
  io_uring_context& context_;
  int fd_;
  offset_t offset_;
  span<std::byte> buffer_;
};

class io_uring_context::write_sender {
  using offset_t = std::int64_t;

  template <typename Receiver>
  class operation : private completion_base {
    friend io_uring_context;

   public:
    template <typename Receiver2>
    explicit operation(const write_sender& sender, Receiver2&& r)
        : context_(sender.context_),
          fd_(sender.fd_),
          offset_(sender.offset_),
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
      static_cast<operation*>(op)->start_io();
    }

    void start_io() noexcept {
      UNIFEX_ASSERT(context_.is_running_on_io_thread());

      auto populateSqe = [this](io_uring_sqe & sqe) noexcept {
        sqe.opcode = IORING_OP_WRITEV;
        sqe.fd = fd_;
        sqe.off = offset_;
        sqe.addr = reinterpret_cast<std::uintptr_t>(&buffer_[0]);
        sqe.len = 1;
        sqe.user_data = reinterpret_cast<std::uintptr_t>(
            static_cast<completion_base*>(this));

        this->execute_ = &operation::on_write_complete;
      };

      if (!context_.try_submit_io(populateSqe)) {
        this->execute_ = &operation::on_schedule_complete;
        context_.schedule_pending_io(this);
      }
    }

    static void on_write_complete(operation_base* op) noexcept {
      auto& self = *static_cast<operation*>(op);
      if (self.result_ >= 0) {
        if constexpr (noexcept(unifex::set_value(std::move(self.receiver_), ssize_t(self.result_)))) {
          unifex::set_value(std::move(self.receiver_), ssize_t(self.result_));
        } else {
          UNIFEX_TRY {
            unifex::set_value(std::move(self.receiver_), ssize_t(self.result_));
          } UNIFEX_CATCH (...) {
            unifex::set_error(std::move(self.receiver_), std::current_exception());
          }
        }
      } else if (self.result_ == -ECANCELED) {
        unifex::set_done(std::move(self.receiver_));
      } else {
        unifex::set_error(
            std::move(self.receiver_),
            std::error_code{-self.result_, std::system_category()});
      }
    }

    io_uring_context& context_;
    int fd_;
    offset_t offset_;
    iovec buffer_[1];
    Receiver receiver_;
  };

 public:
  // Produces number of bytes read.
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<ssize_t>>;

  // Note: Only case it might complete with exception_ptr is if the
  // receiver's set_value() exits with an exception.
  template <template <typename...> class Variant>
  using error_types = Variant<std::error_code, std::exception_ptr>;

  static constexpr bool sends_done = true;

  explicit write_sender(
      io_uring_context& context,
      int fd,
      offset_t offset,
      span<const std::byte> buffer) noexcept
      : context_(context), fd_(fd), offset_(offset), buffer_(buffer) {}

  template <typename Receiver>
  operation<remove_cvref_t<Receiver>> connect(Receiver&& r) {
    return operation<remove_cvref_t<Receiver>>{*this, (Receiver &&) r};
  }

 private:
  io_uring_context& context_;
  int fd_;
  offset_t offset_;
  span<const std::byte> buffer_;
};

class io_uring_context::async_read_only_file {
 public:
  using offset_t = std::int64_t;

  explicit async_read_only_file(io_uring_context& context, int fd) noexcept
      : context_(context), fd_(fd) {}

 private:
  friend scheduler;

  friend read_sender tag_invoke(
      tag_t<async_read_some_at>,
      async_read_only_file& file,
      offset_t offset,
      span<std::byte> buffer) noexcept {
    return read_sender{file.context_, file.fd_.get(), offset, buffer};
  }

  io_uring_context& context_;
  safe_file_descriptor fd_;
};

class io_uring_context::async_write_only_file {
 public:
  using offset_t = std::int64_t;

  explicit async_write_only_file(io_uring_context& context, int fd) noexcept
      : context_(context), fd_(fd) {}

 private:
  friend scheduler;

  friend write_sender tag_invoke(
      tag_t<async_write_some_at>,
      async_write_only_file& file,
      offset_t offset,
      span<const std::byte> buffer) noexcept {
    return write_sender{file.context_, file.fd_.get(), offset, buffer};
  }

  io_uring_context& context_;
  safe_file_descriptor fd_;
};

class io_uring_context::async_read_write_file {
 public:
  using offset_t = std::int64_t;

  explicit async_read_write_file(io_uring_context& context, int fd) noexcept
      : context_(context), fd_(fd) {}

 private:
  friend scheduler;

  friend write_sender tag_invoke(
      tag_t<async_write_some_at>,
      async_read_write_file& file,
      offset_t offset,
      span<const std::byte> buffer) noexcept {
    return write_sender{file.context_, file.fd_.get(), offset, buffer};
  }

  friend read_sender tag_invoke(
      tag_t<async_read_some_at>,
      async_read_write_file& file,
      offset_t offset,
      span<std::byte> buffer) noexcept {
    return read_sender{file.context_, file.fd_.get(), offset, buffer};
  }

  io_uring_context& context_;
  safe_file_descriptor fd_;
};

class io_uring_context::schedule_at_sender {
  template <typename Receiver>
  struct operation : schedule_at_operation {
    static constexpr bool is_stop_ever_possible =
        !is_stop_never_possible_v<stop_token_type_t<Receiver>>;

   public:
    explicit operation(
        io_uring_context& context,
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
        UNIFEX_ASSERT(false);
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

      if constexpr (noexcept(unifex::set_value(std::move(timerOp).receiver_))) {
        unifex::set_value(std::move(timerOp).receiver_);
      } else {
        UNIFEX_TRY {
          unifex::set_value(std::move(timerOp).receiver_);
        } UNIFEX_CATCH (...) {
          unifex::set_error(std::move(timerOp).receiver_, std::current_exception());
        }
      }
    }

    static void remove_timer_from_queue_and_complete_with_done(
        operation_base* op) noexcept {
      // Avoid instantiating set_done() if we're never going to call it.
      if constexpr (is_stop_ever_possible) {
        auto& timerOp = *static_cast<operation*>(op);
        UNIFEX_ASSERT(timerOp.context_.is_running_on_io_thread());

        timerOp.stopCallback_.destruct();

        auto state = timerOp.state_.load(std::memory_order_relaxed);
        if ((state & schedule_at_operation::timer_elapsed_flag) == 0) {
          // Timer not yet removed from the timers_ list. Do that now.
          timerOp.context_.remove_timer(&timerOp);
        }

        unifex::set_done(std::move(timerOp).receiver_);
      } else {
        // Should never be called if stop is not possible.
        UNIFEX_ASSERT(false);
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
      UNIFEX_ASSERT(context_.is_running_on_io_thread());

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

  // Note: Only case it might complete with exception_ptr is if the
  // receiver's set_value() exits with an exception.
  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  explicit schedule_at_sender(
      io_uring_context& context,
      const time_point& dueTime) noexcept
      : context_(context), dueTime_(dueTime) {}

  template <typename Receiver>
  operation<remove_cvref_t<Receiver>> connect(Receiver&& r) {
    return operation<remove_cvref_t<Receiver>>{
        context_, dueTime_, (Receiver &&) r};
  }

 private:
  io_uring_context& context_;
  time_point dueTime_;
};

class io_uring_context::scheduler {
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
  friend io_uring_context;

  friend async_read_only_file tag_invoke(
      tag_t<open_file_read_only>,
      scheduler s,
      const filesystem::path& path);
  friend async_read_write_file tag_invoke(
      tag_t<open_file_read_write>,
      scheduler s,
      const filesystem::path& path);
  friend async_write_only_file tag_invoke(
      tag_t<open_file_write_only>,
      scheduler s,
      const filesystem::path& path);

  friend bool operator==(scheduler a, scheduler b) noexcept {
    return a.context_ == b.context_;
  }
  friend bool operator!=(scheduler a, scheduler b) noexcept {
    return a.context_ != b.context_;
  }

  explicit scheduler(io_uring_context& context) noexcept : context_(&context) {}

  io_uring_context* context_;
};

inline io_uring_context::scheduler io_uring_context::get_scheduler() noexcept {
  return scheduler{*this};
}

} // namespace linuxos
} // namespace unifex

#include <unifex/detail/epilogue.hpp>

#endif // __has_include(<liburing.h>)
