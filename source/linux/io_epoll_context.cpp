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

#if __has_include(<sys/epoll.h>)

#include <unifex/linux/io_epoll_context.hpp>

#include <unifex/scope_guard.hpp>

#include <cassert>
#include <cstring>
#include <system_error>
#include <thread>

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

namespace unifex::linuxos {

static thread_local io_epoll_context* currentThreadContext;

static constexpr __u64 remote_queue_event_user_data = 0;

io_epoll_context::io_epoll_context() {
  count_ = 0;

  {
    int fd = epoll_create(1);
    // LOGX("epoll_create result: %i\n", fd);
    if (fd < 0) {
      int errorCode = errno;
      LOGX("epoll_create failed with %i\n", errorCode);
      throw std::system_error{errorCode, std::system_category()};
    }
    epollFd_ = safe_file_descriptor{fd};
  }

  {
    int fd = timerfd_create(CLOCK_MONOTONIC, 0);
    // LOGX("timerfd_create CLOCK_MONOTONIC result: %i\n", fd);
    if (fd < 0) {
      int errorCode = errno;
      LOGX("timerfd_create CLOCK_MONOTONIC failed with %i\n", errorCode);
      throw std::system_error{errorCode, std::system_category()};
    }

    timerFd_ = safe_file_descriptor{fd};
  }

  {
    epoll_event event;
    event.events = EPOLLIN;
    event.data.u64 = timer_user_data();
    int result = epoll_ctl(epollFd_.get(), EPOLL_CTL_ADD, timerFd_.get(), &event);
    // LOGX("epoll_ctl EPOLL_CTL_ADD timerFd_ result: %i\n", result);
    if (result < 0) {
      int errorCode = errno;
      LOGX("epoll_ctl EPOLL_CTL_ADD timerFd_ failed with %i\n", errorCode);
      throw std::system_error{errorCode, std::system_category()};
    }
  }

  {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    // LOGX("eventfd result: %i\n", fd);
    if (fd < 0) {
      int errorCode = errno;
      LOGX("eventfd failed with %i\n", errorCode);
      throw std::system_error{errorCode, std::system_category()};
    }

    remoteQueueEventFd_ = safe_file_descriptor{fd};
  }

  {
    epoll_event event;
    event.events = EPOLLIN;
    event.data.u64 = remote_queue_event_user_data;
    int result = epoll_ctl(epollFd_.get(), EPOLL_CTL_ADD, remoteQueueEventFd_.get(), &event);
    // LOGX("epoll_ctl EPOLL_CTL_ADD remoteQueueEventFd_ result: %i\n", result);
    if (result < 0) {
      int errorCode = errno;
      LOGX("epoll_ctl EPOLL_CTL_ADD remoteQueueEventFd_ failed with %i\n", errorCode);
      throw std::system_error{errorCode, std::system_category()};
    }
  }

  LOG("io_epoll_context construction done");
}

io_epoll_context::~io_epoll_context() {
  epoll_event event;
  (void)epoll_ctl(epollFd_.get(), EPOLL_CTL_DEL, remoteQueueEventFd_.get(), &event);
  (void)epoll_ctl(epollFd_.get(), EPOLL_CTL_DEL, timerFd_.get(), &event);
  LOG("io_epoll_context destructor done");
}

void io_epoll_context::run_impl(const bool& shouldStop) {
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

    if (localQueue_.empty()) {
      if (!remoteQueueReadSubmitted_) {
        LOG("try_register_remote_queue_notification()");
        remoteQueueReadSubmitted_ = try_register_remote_queue_notification();
      }

      LOG("epoll_wait()");
      int result = epoll_wait(
        epollFd_.get(),
        completed_,
        maxCount_,
        -1);
      if (result < 0) {
        int errorCode = errno;
        throw std::system_error{errorCode, std::system_category()};
      }
      LOGX("epoll_wait() returned %i\n", result);
      count_ = result;
    }
  }
}

bool io_epoll_context::is_running_on_io_thread() const noexcept {
  return this == currentThreadContext;
}

void io_epoll_context::schedule_impl(operation_base* op) {
  assert(op != nullptr);
  if (is_running_on_io_thread()) {
    LOG("schedule_impl - local");
    schedule_local(op);
  } else {
    LOG("schedule_impl - remote");
    schedule_remote(op);
  }
}

void io_epoll_context::schedule_local(operation_base* op) noexcept {
  LOG("schedule_local");
  localQueue_.push_back(op);
}

void io_epoll_context::schedule_local(operation_queue ops) noexcept {
  localQueue_.append(std::move(ops));
}

void io_epoll_context::schedule_remote(operation_base* op) noexcept {
  LOG("schedule_remote");
  bool ioThreadWasInactive = remoteQueue_.enqueue(op);
  if (ioThreadWasInactive) {
    // We were the first to queue an item and the I/O thread is not
    // going to check the queue until we signal it that new items
    // have been enqueued remotely by writing to the eventfd.
    signal_remote_queue();
  }
}

void io_epoll_context::schedule_at_impl(schedule_at_operation* op) noexcept {
  LOG("schedule_at_impl");
  assert(is_running_on_io_thread());
  timers_.insert(op);
  if (timers_.top() == op) {
    timersAreDirty_ = true;
  }
}

void io_epoll_context::execute_pending_local() noexcept {
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

void io_epoll_context::acquire_completion_queue_items() noexcept {

  LOGX("got %u completions\n", count_);

  operation_queue completionQueue;

  for (std::uint32_t i = 0; i < count_; ++i) {
    auto& completed = completed_[i];

    if (completed.data.u64 == remote_queue_event_user_data) {
      LOG("got remote queue wakeup");

      // Read the eventfd to clear the signal.
      __u64 buffer;
      ssize_t bytesRead =
          read(remoteQueueEventFd_.get(), &buffer, sizeof(buffer));
      if (bytesRead < 0) {
        // read() failed
        int errorCode = errno;
        LOGX("read on eventfd failed with %i\n", errorCode);

        std::terminate();
      }

      assert(bytesRead == sizeof(buffer));

      // Skip processing this item and let the loop check
      // for the remote-queued items next time around.
      remoteQueueReadSubmitted_ = false;
      continue;
    } else if (completed.data.u64 == timer_user_data()) {
      // LOGX("got timer completion result %i\n", cqe.res);
      assert(activeTimerCount_ > 0);
      --activeTimerCount_;

      LOGX("now %u active timers\n", activeTimerCount_);
      // if (cqe.res != ECANCELED) {
        LOG("timer not cancelled, marking timers as dirty");
        timersAreDirty_ = true;
      // }

      if (activeTimerCount_ == 0) {
        LOG("no more timers, resetting current due time");
        currentDueTime_.reset();
      }
      continue;
    } else if (completed.data.u64 == remove_timer_user_data()) {
      // Ignore timer cancellation completion.
      continue;
    }

    auto& completionState = *reinterpret_cast<completion_base*>(
        static_cast<std::uintptr_t>(completed.data.u64));

    // Save the result in the completion state.
    // completionState.result_ = cqe.res;

    // Add it to a temporary queue of newly completed items.
    completionQueue.push_back(&completionState);
  }

  schedule_local(std::move(completionQueue));

  // Mark those completion queue entries as consumed.
  count_ = 0;
}

void io_epoll_context::acquire_remote_queued_items() noexcept {
  assert(!remoteQueueReadSubmitted_);
  auto items = remoteQueue_.dequeue_all();
  LOG(items.empty() ? "remote queue is empty"
                    : "acquired items from remote queue");
  schedule_local(std::move(items));
}

bool io_epoll_context::try_register_remote_queue_notification() noexcept {
  auto queuedItems = remoteQueue_.try_mark_inactive_or_dequeue_all();
  LOG(queuedItems.empty() ? "remote queue is empty"
                          : "registered items from remote queue");
  if (!queuedItems.empty()) {
    schedule_local(std::move(queuedItems));
    return false;
  }
  return true;
}

void io_epoll_context::signal_remote_queue() {
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
    throw std::system_error{errorCode, std::system_category()};
  }

  assert(bytesWritten == sizeof(value));
}

void io_epoll_context::remove_timer(schedule_at_operation* op) noexcept {
  LOGX("remove_timer(%p)\n", (void*)op);

  assert(!timers_.empty());
  if (timers_.top() == op) {
    timersAreDirty_ = true;
  }
  timers_.remove(op);
}

void io_epoll_context::update_timers() noexcept {
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
    time_point now = monotonic_clock::now();
    const auto earliestDueTime = timers_.top()->dueTime_;
    LOGX(
      "next timer in %i ms\n",
      (int)std::chrono::duration_cast<std::chrono::milliseconds>(
          earliestDueTime - now)
          .count());
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
        LOG("active timer, already the correct time, nothing to do");
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

bool io_epoll_context::try_submit_timer_io(const time_point& dueTime) noexcept {
  itimerspec time;
	time.it_interval.tv_sec = 0;
	time.it_interval.tv_nsec = 0;
	time.it_value.tv_sec = dueTime.seconds_part();
	time.it_value.tv_nsec = dueTime.nanoseconds_part();
  int result = timerfd_settime(timerFd_.get(), TFD_TIMER_ABSTIME, &time, NULL);
  LOGX("timerfd_settime result - %i\n", result);
  if (result < 0) {
    int errorCode = errno;
    LOGX("timerfd_settime failed with %i\n", errorCode);
    return false;
  }
  ++activeTimerCount_;
  return true;
}

bool io_epoll_context::try_submit_timer_io_cancel() noexcept {
  itimerspec time;
	time.it_interval.tv_sec = 0;
	time.it_interval.tv_nsec = 0;
	time.it_value.tv_sec = 0;
	time.it_value.tv_nsec = 0;
  int result = timerfd_settime(timerFd_.get(), TFD_TIMER_ABSTIME, &time, NULL);
  LOGX("timerfd_settime reset result - %i\n", result);
  if (result < 0) {
    int errorCode = errno;
    LOGX("timerfd_settime reset failed with %i\n", errorCode);
    return false;
  }
  --activeTimerCount_;
  return true;
}

} // namespace unifex::linuxos

#endif // __has_include(<sys/epoll.h>)
