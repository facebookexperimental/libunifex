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
 #include <unifex/static_thread_pool.hpp>

namespace unifex
{

static_thread_pool::static_thread_pool()
: static_thread_pool(std::thread::hardware_concurrency())
{}

static_thread_pool::static_thread_pool(std::uint32_t threadCount) {
    assert(threadCount > 0);

    threads_.reserve(threadCount);
    try {
        threads_.emplace_back([this] { this->run(); });
    } catch (...) {
        close();
    }
}

static_thread_pool::~static_thread_pool() {
    close();
}

void static_thread_pool::run() noexcept {
    std::unique_lock lk{mut_};
    while (true) {
        while (!queue_.empty()) {
            auto* task = queue_.pop_front();
            lk.unlock();
            task->execute(task);
            lk.lock();
        }

        if (stop_) {
            return;
        }

        cv_.wait(lk);
    }
}

void static_thread_pool::close() noexcept {
    {
        std::lock_guard lk{mut_};
        stop_ = true;
        cv_.notify_all();
    }
    for (auto& t : threads_) {
        t.join();
    }
}

void static_thread_pool::enqueue(task_base* task) noexcept {
    std::lock_guard lk{mut_};
    queue_.push_back(task);
    cv_.notify_one();
}

}
