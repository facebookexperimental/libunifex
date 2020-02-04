/*
 * Copyright 2020-present Facebook, Inc.
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

#include <cassert>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace unifex {


class new_thread_context {
private:
  template<typename Receiver>
  class schedule_op {
  public:
    template<typename Receiver2>
    explicit schedule_op(new_thread_context* ctx, Receiver2&& r)
    : ctx_(ctx), receiver_((Receiver2&&)r)
    {
    }

    ~schedule_op() {
        assert(!thread_.joinable());
    }

    void start() & noexcept {
      {
        std::unique_lock lk{ctx_->mut_};
        ++ctx_->activeThreadCount_;
      }

      try {
        std::lock_guard opLock{mut_};
        thread_ = std::thread([this]() noexcept { this->run(); });
      } catch (...) {
        {
          std::lock_guard ctxLock{ctx_->mut_};
          --ctx_->activeThreadCount_;
          // Don't need to signal the condition_variable here as the
          // operation has not yet completed (we haven't called set_error)
          // and so the caller shouldn't be calling the destructor of the
          // new_thread_context yet.
        }
        unifex::set_error(std::move(receiver_), std::current_exception());
      }
    }

  private:
    void run() noexcept {
      std::thread thisThread;
      {
        std::lock_guard opLock{mut_};
        thisThread = std::move(thread_);
      }

      new_thread_context* ctx = ctx_;
      if (get_stop_token(receiver_).stop_requested()) {
        unifex::set_done(std::move(receiver_));
      } else {
        try {
          unifex::set_value(std::move(receiver_));
        } catch (...) {
          unifex::set_error(std::move(receiver_), std::current_exception());
        }
      }

      ctx->retire_thread(std::move(thisThread));
    }

    new_thread_context* ctx_;
    Receiver receiver_;

    std::mutex mut_;
    std::thread thread_;
  };

  class schedule_sender {
  public:
    template<template<typename...> class Variant,
             template<typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template<template<typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    explicit schedule_sender(new_thread_context* ctx) noexcept
    : context_(ctx) {}

    template<typename Receiver>
    schedule_op<std::remove_cvref_t<Receiver>> connect(Receiver&& r) const {
        return schedule_op<std::remove_cvref_t<Receiver>>{context_, (Receiver&&)r};
    }

  private:
    new_thread_context* context_;
  };

  class scheduler {
  public:
    explicit scheduler(new_thread_context* ctx) noexcept : context_(ctx) {}

    schedule_sender schedule() const noexcept {
        return schedule_sender{context_};
    }

  private:
    new_thread_context* context_;
  };

public:
  new_thread_context() = default;

  ~new_thread_context() {
    std::unique_lock lk{mut_};
    cv_.wait(lk, [this] { return activeThreadCount_ == 0; });
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
      if (--activeThreadCount_ == 0) {
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
  size_t activeThreadCount_ = 0;
};

} // namespace unifex
