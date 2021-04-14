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

#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _new_thread {
class context;

template <typename Receiver>
struct _op {
  class type;
};
template <typename Receiver>
using operation = typename _op<remove_cvref_t<Receiver>>::type;

template <typename Receiver>
class _op<Receiver>::type final {
 public:
  template <typename Receiver2>
  explicit type(context* ctx, Receiver2&& r)
    : ctx_(ctx), receiver_((Receiver2&&)r) {}

  ~type() {
      UNIFEX_ASSERT(!thread_.joinable());
  }

  void start() & noexcept;

 private:
  void run() noexcept;

  context* ctx_;
  Receiver receiver_;

  std::mutex mut_;
  std::thread thread_;
};

class context {
private:
  template <class Receiver>
  friend struct _op;

  class schedule_sender {
  public:
    template <template <typename...> class Variant,
             template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    explicit schedule_sender(context* ctx) noexcept
      : context_(ctx) {}

    template <typename Receiver>
    operation<Receiver> connect(Receiver&& r) const {
      return operation<Receiver>{context_, (Receiver&&)r};
    }

  private:
    context* context_;
  };

  class scheduler {
  public:
    explicit scheduler(context* ctx) noexcept : context_(ctx) {}

    schedule_sender schedule() const noexcept {
      return schedule_sender{context_};
    }
    friend bool operator==(scheduler a, scheduler b) noexcept {
      return a.context_ == b.context_;
    }
    friend bool operator!=(scheduler a, scheduler b) noexcept {
      return a.context_ != b.context_;
    }

  private:
    context* context_;
  };

public:
  context() = default;

  ~context() {
    // The activeThreadCount_ counter is initialised to 1 so it will never get to
    // zero until after enter the destructor and decrement the last count here.
    // We do this so that the retire_thread() call doesn't end up calling
    // into the cv_.notify_one() until we are about to start waiting on the
    // cv.
    activeThreadCount_.fetch_sub(1, std::memory_order_relaxed);

    std::unique_lock lk{mut_};
    cv_.wait(lk, [this]() noexcept {
      return activeThreadCount_.load(std::memory_order_relaxed) == 0;
    });
    if (threadToJoin_.joinable()) {
      threadToJoin_.join();
    }
  }

  scheduler get_scheduler() noexcept {
    return scheduler{this};
  }

private:
  void retire_thread(std::thread t) noexcept {
    std::thread prevThread;
    {
      std::lock_guard lk{mut_};
      prevThread = std::exchange(threadToJoin_, std::move(t));
      if (activeThreadCount_.fetch_sub(1, std::memory_order_relaxed) == 1) {
        cv_.notify_one();
      }
    }

    if (prevThread.joinable()) {
      prevThread.join();
    }
  }

  std::mutex mut_;
  std::condition_variable cv_;
  std::thread threadToJoin_;
  std::atomic<size_t> activeThreadCount_ = 1;
};

template <typename Receiver>
inline void _op<Receiver>::type::start() & noexcept {
  UNIFEX_TRY {
    // Acquire the lock before launching the thread.
    // This prevents the run() method from trying to read the thread_ variable
    // until after we have finished assigning it.
    //
    // Note that this thread_ variable is private to this particular operation
    // state and so will only be accessed by this start() method and the run()
    // method.
    std::lock_guard opLock{mut_};
    thread_ = std::thread([this]() noexcept { this->run(); });

    // Now that we've successfully launched the thread, increment the active
    // thread count in the context. Do this before we release the lock so that
    // we ensure the count increment happens before the count decrement that
    // is performed when the thread is being retired.
    ctx_->activeThreadCount_.fetch_add(1, std::memory_order_relaxed);
  } UNIFEX_CATCH (...) {
    unifex::set_error(std::move(receiver_), std::current_exception());
  }
}

template <typename Receiver>
inline void _op<Receiver>::type::run() noexcept {
  // Read the thread_ and ctx_ members out from the operation-state
  // and store them as local variables on the stack before calling the
  // receiver completion-signalling methods as the receiver methods
  // will likely end up destroying the operation-state object before
  // they return.
  context* ctx = ctx_;

  std::thread thisThread;
  {
    // Wait until we can acquire the mutex here.
    // This ensures that the read of thread_ happens-after the write to thread_
    // inside start().
    //
    // TODO: This can be replaced with an atomic<bool>::wait() once we have
    // access to C++20 atomics. This would eliminate the unnecessary synchronisation
    // performed by the unlock() at end-of-scope here.
    std::lock_guard opLock{mut_};
    thisThread = std::move(thread_);
  }

  if (get_stop_token(receiver_).stop_requested()) {
    unifex::set_done(std::move(receiver_));
  } else {
    UNIFEX_TRY {
      unifex::set_value(std::move(receiver_));
    } UNIFEX_CATCH (...) {
      unifex::set_error(std::move(receiver_), std::current_exception());
    }
  }

  ctx->retire_thread(std::move(thisThread));
}

} // namespace _new_thread

using new_thread_context = _new_thread::context;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
