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
#include <unifex/static_thread_pool.hpp>

namespace unifex {
namespace _static_thread_pool {
  context::context()
    : context(std::thread::hardware_concurrency()) {}

  context::context(std::uint32_t threadCount)
    : threadCount_(threadCount)
    , threadStates_(threadCount)
    , nextThread_(0) {
    UNIFEX_ASSERT(threadCount > 0);

    threads_.reserve(threadCount);

    UNIFEX_TRY {
      for (std::uint32_t i = 0; i < threadCount; ++i) {
        threads_.emplace_back([this, i] { run(i); });
      }
    } UNIFEX_CATCH (...) {
      request_stop();
      join();
      UNIFEX_RETHROW();
    }
  }

  context::~context() {
    request_stop();
    join();
  }

  void context::request_stop() noexcept {
    for (auto& state : threadStates_) {
      state.request_stop();
    }
  }

  void context::run(std::uint32_t index) noexcept {
    while (true) {
      task_base* task = nullptr;
      for (std::uint32_t i = 0; i < threadCount_; ++i) {
        auto queueIndex = (index + i) < threadCount_
            ? (index + i)
            : (index + i - threadCount_);
        auto& state = threadStates_[queueIndex];
        task = state.try_pop();
        if (task != nullptr) {
          break;
        }
      }

      if (task == nullptr) {
        task = threadStates_[index].pop();
        if (task == nullptr) {
          // request_stop() was called.
          return;
        }
      }

      task->execute(task);
    }
  }

  void context::join() noexcept {
    for (auto& t : threads_) {
      t.join();
    }
    threads_.clear();
  }

  void context::enqueue(task_base* task) noexcept {
    const std::uint32_t threadCount = static_cast<std::uint32_t>(threads_.size());
    const std::uint32_t startIndex =
        nextThread_.fetch_add(1, std::memory_order_relaxed) % threadCount;

    // First try to enqueue to one of the threads without blocking.
    for (std::uint32_t i = 0; i < threadCount; ++i) {
      const auto index = (startIndex + i) < threadCount
          ? (startIndex + i)
          : (startIndex + i - threadCount);
      if (threadStates_[index].try_push(task)) {
        return;
      }
    }

    // Otherwise, do a blocking enqueue on the selected thread.
    threadStates_[startIndex].push(task);
  }

  task_base* context::thread_state::try_pop() {
    std::unique_lock lk{mut_, std::try_to_lock};
    if (!lk || queue_.empty()) {
      return nullptr;
    }
    return queue_.pop_front();
  }

  task_base* context::thread_state::pop() {
    std::unique_lock lk{mut_};
    while (queue_.empty()) {
      if (stopRequested_) {
        return nullptr;
      }
      cv_.wait(lk);
    }
    return queue_.pop_front();
  }

  bool context::thread_state::try_push(task_base* task) {
    std::unique_lock lk{mut_, std::try_to_lock};
    if (!lk) {
      return false;
    }
    const bool wasEmpty = queue_.empty();
    queue_.push_back(task);
    if (wasEmpty) {
      cv_.notify_one();
    }
    return true;
  }

  void context::thread_state::push(task_base* task) {
    std::lock_guard lk{mut_};
    const bool wasEmpty = queue_.empty();
    queue_.push_back(task);
    if (wasEmpty) {
      cv_.notify_one();
    }
  }

  void context::thread_state::request_stop() {
    std::lock_guard lk{mut_};
    stopRequested_ = true;
    cv_.notify_one();
  }

} // namespace _static_thread_pool
} // namespace unifex
