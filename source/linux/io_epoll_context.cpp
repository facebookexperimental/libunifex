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

#include <unifex/config.hpp>
#if !UNIFEX_NO_EPOLL

#include <unifex/linux/io_epoll_context.hpp>

#include <unifex/scope_guard.hpp>

#include <cassert>
#include <cstring>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>

// #define LOGGING_ENABLED

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

static constexpr void* remote_queue_event_user_data = nullptr;

static constexpr std::uint32_t io_epoll_max_event_count = 256;

io_epoll_context::io_epoll_context() {
  {
    int fd = epoll_create(1);
    if (fd < 0) {
      int errorCode = errno;
      LOGX("epoll_create failed with %i\n", errorCode);
      throw std::system_error{errorCode, std::system_category()};
    }
    epollFd_ = safe_file_descriptor{fd};
  }

  {
    int fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (fd < 0) {
      int errorCode = errno;
      LOGX("timerfd_create CLOCK_MONOTONIC failed with %i\n", errorCode);
      throw std::system_error{errorCode, std::system_category()};
    }

    timerFd_ = safe_file_descriptor{fd};
  }

  {
    epoll_event event = {};
    event.events = EPOLLIN;
    event.data.ptr = timer_user_data();
    int result = epoll_ctl(epollFd_.get(), EPOLL_CTL_ADD, timerFd_.get(), &event);
    if (result < 0) {
      int errorCode = errno;
      LOGX("epoll_ctl EPOLL_CTL_ADD timerFd_ failed with %i\n", errorCode);
      throw std::system_error{errorCode, std::system_category()};
    }
  }

  {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd < 0) {
      int errorCode = errno;
      LOGX("eventfd failed with %i\n", errorCode);
      throw std::system_error{errorCode, std::system_category()};
    }

    remoteQueueEventFd_ = safe_file_descriptor{fd};
  }

  {
    epoll_event event = {};
    event.events = EPOLLIN;
    event.data.ptr = remote_queue_event_user_data;
    int result = epoll_ctl(epollFd_.get(), EPOLL_CTL_ADD, remoteQueueEventFd_.get(), &event);
    if (result < 0) {
      int errorCode = errno;
      LOGX("epoll_ctl EPOLL_CTL_ADD remoteQueueEventFd_ failed with %i\n", errorCode);
      throw std::system_error{errorCode, std::system_category()};
    }
  }

  LOG("io_epoll_context construction done");
}

io_epoll_context::~io_epoll_context() {
  epoll_event event = {};
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

    if (timersAreDirty_) {
      update_timers();
    }

    // Check for remotely-queued items.
    // Only do this if we haven't submitted a poll operation for the
    // completion queue - in which case we'll just wait until we receive the
    // completion-queue item).
    if (!remoteQueueReadSubmitted_) {
      LOG("try_schedule_local_remote_queue_contents()");
      remoteQueueReadSubmitted_ = try_schedule_local_remote_queue_contents();
    }

    if (remoteQueueReadSubmitted_) {
      // Check for any new completion-queue items.
      acquire_completion_queue_items();
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
  assert(op->execute_);
  localQueue_.push_back(op);
}

void io_epoll_context::schedule_local(operation_queue ops) noexcept {
  localQueue_.append(std::move(ops));
}

void io_epoll_context::schedule_remote(operation_base* op) noexcept {
  LOG("schedule_remote");
  assert(op->execute_);
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
    if (item->execute_) {
      item->execute_(item);
    }
    ++count;
  }

  LOGX("processed %zu local queue items\n", count);
}

void io_epoll_context::acquire_completion_queue_items() {

  LOG("epoll_wait()");

  epoll_event completions[io_epoll_max_event_count];
  int result = epoll_wait(
    epollFd_.get(),
    completions,
    io_epoll_max_event_count,
    localQueue_.empty() ? -1 : 0);
  if (result < 0) {
    int errorCode = errno;
    throw std::system_error{errorCode, std::system_category()};
  }
  std::uint32_t count = result;

  LOGX("got %u completions\n", count);

  operation_queue completionQueue;

  for (std::uint32_t i = 0; i < count; ++i) {
    auto& completed = completions[i];

    if (completed.data.ptr == remote_queue_event_user_data) {
      LOG("got remote queue wakeup");

      // Read the eventfd to clear the signal.
      std::uint64_t buffer;
      ssize_t bytesRead =
          read(remoteQueueEventFd_.get(), &buffer, sizeof(buffer));
      if (bytesRead < 0) {
        // read() failed
        [[maybe_unused]] int errorCode = errno;
        LOGX("read on eventfd failed with %i\n", errorCode);

        std::terminate();
      }

      assert(bytesRead == sizeof(buffer));

      // Skip processing this item and let the loop check
      // for the remote-queued items next time around.
      remoteQueueReadSubmitted_ = false;
      continue;
    } else if (completed.data.ptr == timer_user_data()) {
      LOG("got timer wakeup");
      currentDueTime_.reset();
      timersAreDirty_ = true;

      // Read the eventfd to clear the signal.
      std::uint64_t buffer;
      ssize_t bytesRead =
          read(timerFd_.get(), &buffer, sizeof(buffer));
      if (bytesRead < 0) {
        // read() failed
        [[maybe_unused]] int errorCode = errno;
        LOGX("read on timerfd failed with %i\n", errorCode);

        std::terminate();
      }

      assert(bytesRead == sizeof(buffer));
      continue;
    }

    LOGX("completion event %i\n", completed.events);
    auto& completionState = *reinterpret_cast<completion_base*>(completed.data.ptr);

    // Save the result in the completion state.
    // completionState.result_ = cqe.res;

    // Add it to a temporary queue of newly completed items.
    completionQueue.push_back(&completionState);
  }

  schedule_local(std::move(completionQueue));
}

bool io_epoll_context::try_schedule_local_remote_queue_contents() noexcept {
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
  const std::uint64_t value = 1;
  ssize_t bytesWritten =
      write(remoteQueueEventFd_.get(), &value, sizeof(value));
  if (bytesWritten < 0) {
    // What to do here? Terminate/abort/ignore?
    // Try to dequeue the item before returning?
    [[maybe_unused]] int errorCode = errno;
    LOGX("writing to remote queue eventfd failed with %i\n", errorCode);

    std::terminate();
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
      currentDueTime_.reset();
      try_submit_timer_io(time_point{});
      timersAreDirty_ = false;
    }
  } else {
    const auto earliestDueTime = timers_.top()->dueTime_;
    LOGX(
      "next timer in %i ms\n",
      (int)std::chrono::duration_cast<std::chrono::milliseconds>(
          earliestDueTime - monotonic_clock::now())
          .count());
    if (currentDueTime_) {
      constexpr auto threshold = std::chrono::microseconds(1);
      if (earliestDueTime < (*currentDueTime_ - threshold)) {
        LOG("active timer, need to cancel and submit an earlier one");

        // An earlier time has been scheduled.
        // Cancel the old timer before submitting a new one.
        currentDueTime_.reset();
        if (try_submit_timer_io(earliestDueTime)) {
          currentDueTime_ = earliestDueTime;
          timersAreDirty_ = false;
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
  itimerspec time = {};
  time.it_interval.tv_sec = 0;
  time.it_interval.tv_nsec = 0;
  time.it_value.tv_sec = dueTime.seconds_part();
  time.it_value.tv_nsec = dueTime.nanoseconds_part();
  int result = timerfd_settime(timerFd_.get(), TFD_TIMER_ABSTIME, &time, NULL);
  if (result < 0) {
    [[maybe_unused]] int errorCode = errno;
    LOGX("timerfd_settime failed with %i\n", errorCode);
    return false;
  }
  return true;
}

std::pair<io_epoll_context::async_reader, io_epoll_context::async_writer> tag_invoke(
    tag_t<open_pipe>,
    io_epoll_context::scheduler scheduler) {
  int fd[2] = {};
  int result = ::pipe2(fd, O_NONBLOCK | O_CLOEXEC);
  if (result < 0) {
    int errorCode = errno;
    throw std::system_error{errorCode, std::system_category()};
  }

  return {io_epoll_context::async_reader{*scheduler.context_, fd[0]}, io_epoll_context::async_writer{*scheduler.context_, fd[1]}};
}

} // namespace unifex::linuxos

#endif // !UNIFEX_NO_EPOLL
