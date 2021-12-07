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

#include <unifex/config.hpp>

#if !UNIFEX_NO_LIBURING

#include <unifex/linux/io_uring_context.hpp>

#include <unifex/scope_guard.hpp>
#include <unifex/exception.hpp>

#include "io_uring_syscall.hpp"

#include <cstring>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>

//#define LOGGING_ENABLED

#ifdef LOGGING_ENABLED
#define LOG(S)             \
  do {                     \
    ::std::puts(S);        \
    ::std::fflush(stdout); \
  } while (false)
#define LOGX(...)               \
  do {                          \
    ::std::printf(__VA_ARGS__); \
    ::std::fflush(stdout);      \
  } while (false)
#else
#define LOG(S) \
  do {         \
  } while (false)
#define LOGX(...) \
  do {            \
  } while (false)
#endif

/////////////////////////////////////////////////////
// io_uring structures
//
// An io_uring is a set of two circular buffers that are used for sumbmitting
// asynchronous operations to the kernel and receiving completion notifications.
//
// To start an asynchronous operation the I/O thread writes an entry to the
// "submission queue" ring buffer containing the parameters for the syscall.
//
// When the operation completes, the kernel will write a completion notification
// to the "completion queue". These do not necessarily complete in the same
// order that they were submitted.
//
// Note that both ring buffers have a power-of-two size and use separate
// atomic head/tail indices to indicate the front/back of the queue
// respectively. These values must be masked
//
// 'head' contains the index of the front of the queue.
// Consumers should read this entry and advance 'head' once they have finished
// processing it.
//
// 'tail' contains the index of the next slot to be written to.
// It is one-past-the-end of the set of valid entries in the ring-buffer.
// Producers should write to this entry and advance 'tail' once written to
// publish the entry.
//
// Constraints:
// - head <= tail
// - tail - head <= entry-count
//
// submission queue
// ----------------
// // 64-byte structure (fits in a single cache-line)
// struct io_uring_sqe {
//   __u8  opcode;   /* type of operation for this sqe */
//   __u8  flags;    /* IOSQE_ flags */
//   __u16 ioprio;   /* ioprio for the request */
//   __s32 fd;   /* file descriptor to do IO on */
//   union {
//     __u64 off;  /* offset into file */
//     __u64 addr2;
//   };
//   union {
//     __u64 addr; /* pointer to buffer or iovecs */
//     __u64 splice_off_in;
//   };
//   __u32 len;    /* buffer size or number of iovecs */
//   union {
//     __kernel_rwf_t  rw_flags;
//     __u32   fsync_flags;
//     __u16   poll_events;  /* compatibility */
//     __u32   poll32_events;  /* word-reversed for BE */
//     __u32   sync_range_flags;
//     __u32   msg_flags;
//     __u32   timeout_flags;
//     __u32   accept_flags;
//     __u32   cancel_flags;
//     __u32   open_flags;
//     __u32   statx_flags;
//     __u32   fadvise_advice;
//     __u32   splice_flags;
//     __u32   rename_flags;
//     __u32   unlink_flags;
//     __u32   hardlink_flags;
//   };
//   __u64 user_data;  /* data to be passed back at completion time */
//   /* pack this to avoid bogus arm OABI complaints */
//   union {
//     /* index into fixed buffers, if used */
//     __u16 buf_index;
//     /* for grouped buffer selection */
//     __u16 buf_group;
//   } __attribute__((packed));
//   /* personality to use, if used */
//   __u16 personality;
//   union {
//     __s32 splice_fd_in;
//     __u32 file_index;
//   };
//   __u64 __pad2[2];
// };
//
// io_uring_params
// - sq_entries - number of entires in the submission queue
// - sq_off
//   __u32 head;          - offset of ring head index (first valid )
//   __u32 tail;
//   __u32 ring_mask;
//   __u32 ring_entries;
//   __u32 flags;
//   __u32 dropped;
//   __u32 array;
//   __u32 resv1;
//   __u32 resv2;
//
// The submission queue has two memory-mapped regions. One for the control
// data for the queue and another for the actual submission queue entries.
//
// Memory mapped region for io_uring fd at offset IORING_OFF_SQ_RING
//
//                                     SQ index array (unsigned)
// +-----------------------------------------+--------------------+
// |  |head|    |tail|   |dropped|  |flags|  |  |  |  |  ....  |  |
// +-----------------------------------------+--------------------+
//    ^         ^        ^          ^        ^
//    |         |        |          |        |
//  sq_off.head |   sq_off.dropped  |        |
//              |                   |      sq_off.array
//           sq_off.tail        sq_off.flags
//
// Memory mapped region for io_uring fd at offset IORING_OFF_SQES
//
// SQE array (io_uring_sqe)
// +-------------------------+
// |   |   |   |   | ... |   |
// +-------------------------+
//
// Elements of the SQ index array are indices into the SQE array.
//
// When submitting an I/O request you need to:
// - write to a free entry in SQE array
// - write the index of that entry into the SQ index array at offset 'tail'
// - advance 'tail' to publish the entry
//
// Then, depending on which mode the io_uring is operating in, you may need
// to flush these entries by calling io_uring_enter(), passing the number of
// entries to flush.
//
// If the io_uring was created with the flag IOURING_SETUP_SQPOLL then the
// kernel will create a thread the poll for new SQEs. The kernel thread will
// spin waiting for new SQEs to be published for a while and will then go to
// sleep.
// If the kernel thread goes to sleep it will set the IORING_SQ_NEED_WAKEUP
// flag in the sqring's 'flags' field. Applications must check for this flag
// and call io_uring_enter() with IORING_ENTER_SQ_WAKEUP flag set in the flags
// parameter to wake up the kernel thread to start processing items again.
//
// completion queue
// ----------------
// struct io_uring_cqe {
//   __u64 user_data; // arbitrary user-data from submission
//   __s32 res;       // result, return-value of syscall
//   __u32 flags;     // metadata - currently unused?
// };
//
// io_uring_params
// - cq_entries - number of entries in the completion queue
// - cq_off
//   __u32 head;          - offset of ring head (first valid entry)
//   __u32 tail;          - offset of ring tail (last valid entry)
//   __u32 ring_mask;     - mask to apply to head/tail to get array index
//   __u32 ring_entries;  - number of entries in ring (same as cq_entries?)
//   __u32 overflow;
//   __u32 cqes;          -
//   __u64 resv[2];
//
// Memory mapped region for io_uring fd at offset IORING_OFF_CQ_RING
//
//                                     CQE array (io_uring_cqe)
// +-----------------------------------+--------------------+
// |  |head|    |tail|   |overflow|    |  |  |  |  ....  |  |
// +-----------------------------------+--------------------+
//    ^         ^        ^             ^
//    |         |        |             |
//  cq_off.head |   cq_off.overflow    |
//              |                  cq_off.cqes
//           cq_off.tail
//
// When an operation completes the kernel will write an entry to the next
// free slot (at index 'tail') in the CQE array and then publish this entry
// by incrementing 'tail'.
//
// An application should periodically poll 'head' and 'tail' to detect new
// entries being added and advance 'head' once they have finished processing
// the new entries.
//
// If the application otherwise does not have anything to do on the I/O thread
// then it can block, waiting for new entries to be added by calling
// io_uring_enter(), passing the IORING_ENTER_GETEVENTS flag and passing a
// non-zero value for 'min_complete'.
//
//
// Structure of the io_uring_context
// ---------------------------------
// Assumes a single thread that submits I/O and processes completion events.
// This is the thread that calls run().
//
// When a thread schedules work using 'schedule()' it takes one of two paths
// depending on whether it is being submitted from the I/O thread or a remote
// thread.
//
// If it is submitted from the I/O thread then it is just immediately appended
// to the io_uring_context's queue of ready-to-run operations. It will be
// processed next time around the run() loop.
//
// If it is submitted from a remote thread then we perform a lock-free push
// of the item onto a queue of remotely scheduled items.
//
// If the I/O thread becomes idle then it marks the queue with an
// 'inactive consumer' flag and submits an IORING_OP_POLL_ADD operation on an
// eventfd object.
//
// The next time a remote thread enqueues an item to the queue it will see and
// clear this 'inactive consumer' flag and then signal the eventfd by writing
// to it to wake-up the I/O thread.
//
// This will cause an I/O completion event for the POLL operation to be posted
// to the completion queue and wake-up the I/O thread which will then acquire
// the list of remotely scheduled items and add them to the list of
// ready-to-run operations.

namespace unifex::linuxos {

static thread_local io_uring_context* currentThreadContext;

static constexpr __u64 remote_queue_event_user_data = 0;

io_uring_context::io_uring_context() {
  io_uring_params params;
  std::memset(&params, 0, sizeof(params));

  int ret = io_uring_setup(256, &params);
  if (ret < 0) {
    throw_(std::system_error{-ret, std::system_category()});
  }
  iouringFd_ = safe_file_descriptor{ret};

  {
    auto cqSize = params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe);
    void* cqPtr = mmap(
        0,
        cqSize,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_POPULATE,
        iouringFd_.get(),
        IORING_OFF_CQ_RING);
    if (cqPtr == MAP_FAILED) {
      int errorCode = errno;
      throw_(std::system_error{errorCode, std::system_category()});
    }

    cqMmap_ = mmap_region{cqPtr, cqSize};

    char* cqBlock = static_cast<char*>(cqPtr);
    cqEntryCount_ = params.cq_entries;
    UNIFEX_ASSERT(
        cqEntryCount_ ==
        *reinterpret_cast<unsigned*>(
            cqBlock +
            params.cq_off.ring_entries)); // Is this a valid assumption?
    cqMask_ = *reinterpret_cast<unsigned*>(cqBlock + params.cq_off.ring_mask);
    UNIFEX_ASSERT(cqMask_ == (cqEntryCount_ - 1));
    cqHead_ =
        reinterpret_cast<std::atomic<unsigned>*>(cqBlock + params.cq_off.head);
    cqTail_ =
        reinterpret_cast<std::atomic<unsigned>*>(cqBlock + params.cq_off.tail);
    cqOverflow_ = reinterpret_cast<std::atomic<unsigned>*>(
        cqBlock + params.cq_off.overflow);
    cqEntries_ = reinterpret_cast<io_uring_cqe*>(cqBlock + params.cq_off.cqes);
  }

  {
    auto sqSize = params.sq_off.array + params.sq_entries * sizeof(__u32);
    void* sqPtr = mmap(
        0,
        sqSize,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_POPULATE,
        iouringFd_.get(),
        IORING_OFF_SQ_RING);
    if (sqPtr == MAP_FAILED) {
      int errorCode = errno;
      throw_(std::system_error{errorCode, std::system_category()});
    }

    sqMmap_ = mmap_region{sqPtr, sqSize};

    char* sqBlock = static_cast<char*>(sqPtr);

    sqEntryCount_ = params.sq_entries;
    UNIFEX_ASSERT(
        sqEntryCount_ ==
        *reinterpret_cast<unsigned*>(
            sqBlock +
            params.sq_off.ring_entries)); // Is this a valid assumption?
    sqMask_ = *reinterpret_cast<unsigned*>(sqBlock + params.sq_off.ring_mask);
    UNIFEX_ASSERT(sqMask_ == (sqEntryCount_ - 1));
    sqHead_ =
        reinterpret_cast<std::atomic<unsigned>*>(sqBlock + params.sq_off.head);
    sqTail_ =
        reinterpret_cast<std::atomic<unsigned>*>(sqBlock + params.sq_off.tail);
    sqFlags_ =
        reinterpret_cast<std::atomic<unsigned>*>(sqBlock + params.sq_off.flags);
    sqDropped_ = reinterpret_cast<std::atomic<unsigned>*>(
        sqBlock + params.sq_off.dropped);
    sqIndexArray_ = reinterpret_cast<unsigned*>(sqBlock + params.sq_off.array);
  }

  {
    auto sqeSize = params.sq_entries * sizeof(io_uring_sqe);
    void* sqePtr = mmap(
        0,
        sqeSize,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_POPULATE,
        iouringFd_.get(),
        IORING_OFF_SQES);
    if (sqePtr == MAP_FAILED) {
      int errorCode = errno;
      throw_(std::system_error{errorCode, std::system_category()});
    }

    sqeMmap_ = mmap_region{sqePtr, sqeSize};
    sqEntries_ = reinterpret_cast<io_uring_sqe*>(sqePtr);
  }

  {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd < 0) {
      int errorCode = errno;
      throw_(std::system_error{errorCode, std::system_category()});
    }

    remoteQueueEventFd_ = safe_file_descriptor{fd};
  }

  LOG("io_uring_context construction done");
}

io_uring_context::~io_uring_context() {}

void io_uring_context::run_impl(const bool& shouldStop) {
  LOG("run loop started");

  auto* oldContext = std::exchange(currentThreadContext, this);
  scope_guard g = [=]() noexcept {
    std::exchange(currentThreadContext, oldContext);
    LOG("run loop exited");
  };

  while (true) {
    // Dequeue and process local queue items (ready to run)
    execute_pending_local();

    if (shouldStop) {
      break;
    }

    // Check for any new completion-queue items.
    acquire_completion_queue_items();

    if (timersAreDirty_) {
      update_timers();
    }

    // Check for remotely-queued items.
    // Only do this if we haven't submitted a poll operation for the
    // completion queue - in which case we'll just wait until we receive the
    // completion-queue item).
    if (!remoteQueueReadSubmitted_) {
      acquire_remote_queued_items();
    }

    // Process additional I/O requests that were waiting for
    // additional space either in the submission queue or the completion queue.
    while (!pendingIoQueue_.empty() && can_submit_io()) {
      auto* item = pendingIoQueue_.pop_front();
      item->execute_(item);
    }

    if (localQueue_.empty() || sqUnflushedCount_ > 0) {
      const bool isIdle = sqUnflushedCount_ == 0 && localQueue_.empty();
      if (isIdle) {
        if (!remoteQueueReadSubmitted_) {
          LOG("try_register_remote_queue_notification()");
          remoteQueueReadSubmitted_ = try_register_remote_queue_notification();
        }
      }

      int minCompletionCount = 0;
      unsigned flags = 0;
      if (isIdle &&
          (remoteQueueReadSubmitted_ ||
           pending_operation_count() == cqEntryCount_)) {
        // No work to do until we receive a completion event.
        minCompletionCount = 1;
        flags = IORING_ENTER_GETEVENTS;
      }

      LOGX(
          "io_uring_enter() - submit %u, wait for %i, pending %u\n",
          sqUnflushedCount_,
          minCompletionCount,
          pending_operation_count());

      int result = io_uring_enter(
          iouringFd_.get(),
          sqUnflushedCount_,
          minCompletionCount,
          flags,
          nullptr);
      if (result < 0) {
        int errorCode = errno;
        throw_(std::system_error{errorCode, std::system_category()});
      }

      LOG("io_uring_enter() returned");

      sqUnflushedCount_ -= result;
      cqPendingCount_ += result;
    }
  }
}

bool io_uring_context::is_running_on_io_thread() const noexcept {
  return this == currentThreadContext;
}

void io_uring_context::schedule_impl(operation_base* op) {
  UNIFEX_ASSERT(op != nullptr);
  if (is_running_on_io_thread()) {
    schedule_local(op);
  } else {
    schedule_remote(op);
  }
}

void io_uring_context::schedule_local(operation_base* op) noexcept {
  localQueue_.push_back(op);
}

void io_uring_context::schedule_local(operation_queue ops) noexcept {
  localQueue_.append(std::move(ops));
}

void io_uring_context::schedule_remote(operation_base* op) noexcept {
  bool ioThreadWasInactive = remoteQueue_.enqueue(op);
  if (ioThreadWasInactive) {
    // We were the first to queue an item and the I/O thread is not
    // going to check the queue until we signal it that new items
    // have been enqueued remotely by writing to the eventfd.
    signal_remote_queue();
  }
}

void io_uring_context::schedule_pending_io(operation_base* op) noexcept {
  UNIFEX_ASSERT(is_running_on_io_thread());
  pendingIoQueue_.push_back(op);
}

void io_uring_context::reschedule_pending_io(operation_base* op) noexcept {
  UNIFEX_ASSERT(is_running_on_io_thread());
  pendingIoQueue_.push_front(op);
}

void io_uring_context::schedule_at_impl(schedule_at_operation* op) noexcept {
  UNIFEX_ASSERT(is_running_on_io_thread());
  timers_.insert(op);
  if (timers_.top() == op) {
    timersAreDirty_ = true;
  }
}

void io_uring_context::execute_pending_local() noexcept {
  if (localQueue_.empty()) {
    LOG("local queue is empty");
    return;
  }

  LOG("processing local queue items");

  size_t count = 0;
  auto pending = std::move(localQueue_);
  while (!pending.empty()) {
    auto* item = pending.pop_front();
    item->execute_(item);
    ++count;
  }

  LOGX("processed %zu local queue items\n", count);
}

void io_uring_context::acquire_completion_queue_items() noexcept {
  // Use 'relaxed' load for the head since it should only ever
  // be modified by the current thread.
  std::uint32_t cqHead = cqHead_->load(std::memory_order_relaxed);
  std::uint32_t cqTail = cqTail_->load(std::memory_order_acquire);
  LOGX("completion queue head = %u, tail = %u\n", cqHead, cqTail);

  if (cqHead != cqTail) {
    const auto mask = cqMask_;
    const auto count = cqTail - cqHead;
    UNIFEX_ASSERT(count <= cqEntryCount_);

    operation_base head;

    LOGX("got %u completions\n", count);

    operation_queue completionQueue;

    for (std::uint32_t i = 0; i < count; ++i) {
      auto& cqe = cqEntries_[(cqHead + i) & mask];

      if (cqe.user_data == remote_queue_event_user_data) {
        LOG("got remote queue wakeup");
        if (cqe.res < 0) {
          LOGX("remote queue wakeup failed err: %i\n", cqe.res);

          // readv() operation failed.
          // TODO: What to do here?
          std::terminate();
        }

        // Read the eventfd to clear the signal.
        __u64 buffer;
        ssize_t bytesRead =
            read(remoteQueueEventFd_.get(), &buffer, sizeof(buffer));
        if (bytesRead < 0) {
          // read() failed
          [[maybe_unused]] int errorCode = errno;
          LOGX("read on eventfd failed with %i\n", errorCode);

          std::terminate();
        }

        UNIFEX_ASSERT(bytesRead == sizeof(buffer));

        // Skip processing this item and let the loop check
        // for the remote-queued items next time around.
        remoteQueueReadSubmitted_ = false;
        continue;
      } else if (cqe.user_data == timer_user_data()) {
        LOGX("got timer completion result %i\n", cqe.res);
        UNIFEX_ASSERT(activeTimerCount_ > 0);
        --activeTimerCount_;

        LOGX("now %u active timers\n", activeTimerCount_);
        if (cqe.res != ECANCELED) {
          LOG("timer not cancelled, marking timers as dirty");
          timersAreDirty_ = true;
        }

        if (activeTimerCount_ == 0) {
          LOG("no more timers, resetting current due time");
          currentDueTime_.reset();
        }
        continue;
      } else if (cqe.user_data == remove_timer_user_data()) {
        // Ignore timer cancellation completion.
        continue;
      }

      auto& completionState = *reinterpret_cast<completion_base*>(
          static_cast<std::uintptr_t>(cqe.user_data));

      // Save the result in the completion state.
      completionState.result_ = cqe.res;

      // Add it to a temporary queue of newly completed items.
      completionQueue.push_back(&completionState);
    }

    schedule_local(std::move(completionQueue));

    // Mark those completion queue entries as consumed.
    cqHead_->store(cqTail, std::memory_order_release);
    cqPendingCount_ -= count;
  }
}

void io_uring_context::acquire_remote_queued_items() noexcept {
  UNIFEX_ASSERT(!remoteQueueReadSubmitted_);
  auto items = remoteQueue_.dequeue_all();
  LOG(items.empty() ? "remote queue is empty"
                    : "acquired items from remote queue");
  schedule_local(std::move(items));
}

bool io_uring_context::try_register_remote_queue_notification() noexcept {
  // Check that we haven't already hit the limit of pending
  // I/O completion events.
  const auto populateRemoteQueuePollSqe = [this](io_uring_sqe & sqe) noexcept {
    auto queuedItems = remoteQueue_.try_mark_inactive_or_dequeue_all();
    if (!queuedItems.empty()) {
      schedule_local(std::move(queuedItems));
      return false;
    }

    sqe.opcode = IORING_OP_POLL_ADD;
    sqe.fd = remoteQueueEventFd_.get();
    sqe.poll_events = POLL_IN;
    sqe.user_data = remote_queue_event_user_data;

    return true;
  };

  if (try_submit_io(populateRemoteQueuePollSqe)) {
    LOG("added eventfd poll to submission queue");
    return true;
  }

  return false;
}

void io_uring_context::signal_remote_queue() {
  LOG("writing bytes to eventfd");

  // Notify eventfd() by writing a 64-bit integer to it.
  const __u64 value = 1;
  ssize_t bytesWritten =
      write(remoteQueueEventFd_.get(), &value, sizeof(value));
  if (bytesWritten < 0) {
    // What to do here? Terminate/abort/ignore?
    // Try to dequeue the item before returning?
    LOG("error writing to remote queue eventfd");
    int errorCode = errno;
    throw_(std::system_error{errorCode, std::system_category()});
  }

  UNIFEX_ASSERT(bytesWritten == sizeof(value));
}

void io_uring_context::remove_timer(schedule_at_operation* op) noexcept {
  LOGX("remove_timer(%p)\n", (void*)op);

  UNIFEX_ASSERT(!timers_.empty());
  if (timers_.top() == op) {
    timersAreDirty_ = true;
  }
  timers_.remove(op);
}

void io_uring_context::update_timers() noexcept {
  LOG("update_timers()");

  // Reap any elapsed timers.
  if (!timers_.empty()) {
    time_point now = monotonic_clock::now();
    while (!timers_.empty() && timers_.top()->dueTime_ <= now) {
      schedule_at_operation* item = timers_.pop();

      LOGX("dequeued elapsed timer %p\n", (void*)item);

      if (item->canBeCancelled_) {
        auto oldState = item->state_.fetch_add(
            schedule_at_operation::timer_elapsed_flag,
            std::memory_order_acq_rel);
        if ((oldState & schedule_at_operation::cancel_pending_flag) != 0) {
          LOGX("timer already cancelled\n");

          // Timer has been cancelled by a remote thread.
          // The other thread is responsible for enqueueing is operation onto
          // the remoteQueue_.
          continue;
        }
      }

      // Otherwise, we are responsible for enqueuing the timer onto the
      // ready-to-run queue.
      schedule_local(item);
    }
  }

  // Check if we need to cancel or start some new OS timers.
  if (timers_.empty()) {
    if (currentDueTime_.has_value()) {
      LOG("no more schedule_at requests, cancelling timer");

      // Cancel the outstanding timer.
      if (try_submit_timer_io_cancel()) {
        currentDueTime_.reset();
        timersAreDirty_ = false;
      }
    }
  } else {
    const auto earliestDueTime = timers_.top()->dueTime_;

    if (currentDueTime_) {
      constexpr auto threshold = std::chrono::microseconds(1);
      if (earliestDueTime < (*currentDueTime_ - threshold)) {
        LOG("active timer, need to cancel and submit an earlier one");

        // An earlier time has been scheduled.
        // Cancel the old timer before submitting a new one.
        if (try_submit_timer_io_cancel()) {
          currentDueTime_.reset();
          if (try_submit_timer_io(earliestDueTime)) {
            currentDueTime_ = earliestDueTime;
            timersAreDirty_ = false;
          }
        }
      } else {
        timersAreDirty_ = false;
      }
    } else {
      // No active timer, submit a new timer
      LOG("no active timer, trying to submit a new one");
      if (try_submit_timer_io(earliestDueTime)) {
        currentDueTime_ = earliestDueTime;
        timersAreDirty_ = false;
      }
    }
  }
}

bool io_uring_context::try_submit_timer_io(const time_point& dueTime) noexcept {
  auto populateSqe = [&](io_uring_sqe & sqe) noexcept {
    sqe.opcode = IORING_OP_TIMEOUT;
    sqe.addr = reinterpret_cast<std::uintptr_t>(&time_);
    sqe.len = 1;
    sqe.rw_flags =
        1; // HACK: Should be 'sqe.timeout_flags = IORING_TIMEOUT_ABS'
    sqe.user_data = timer_user_data();

    time_.tv_sec = dueTime.seconds_part();
    time_.tv_nsec = dueTime.nanoseconds_part();
  };

  if (try_submit_io(populateSqe)) {
    ++activeTimerCount_;
    return true;
  }

  return false;
}

bool io_uring_context::try_submit_timer_io_cancel() noexcept {
  auto populateSqe = [&](io_uring_sqe & sqe) noexcept {
    sqe.opcode = 12; // IORING_OP_TIMEOUT_REMOVE;
    sqe.addr = timer_user_data();
    sqe.user_data = remove_timer_user_data();
  };

  return try_submit_io(populateSqe);
}

io_uring_context::async_read_only_file tag_invoke(
    tag_t<open_file_read_only>,
    io_uring_context::scheduler scheduler,
    const filesystem::path& path) {
  int result = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (result < 0) {
    int errorCode = errno;
    throw_(std::system_error{errorCode, std::system_category()});
  }

  return io_uring_context::async_read_only_file{*scheduler.context_, result};
}

io_uring_context::async_write_only_file tag_invoke(
    tag_t<open_file_write_only>,
    io_uring_context::scheduler scheduler,
    const filesystem::path& path) {
  int result = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  if (result < 0) {
    int errorCode = errno;
    throw_(std::system_error{errorCode, std::system_category()});
  }

  return io_uring_context::async_write_only_file{*scheduler.context_, result};
}

io_uring_context::async_read_write_file tag_invoke(
    tag_t<open_file_read_write>,
    io_uring_context::scheduler scheduler,
    const filesystem::path& path) {
  int result = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (result < 0) {
    int errorCode = errno;
    throw_(std::system_error{errorCode, std::system_category()});
  }

  return io_uring_context::async_read_write_file{*scheduler.context_, result};
}

} // namespace unifex::linuxos

#endif // UNIFEX_NO_LIBURING
