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

#include <unifex/manual_event_loop.hpp>

#include <thread>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _single_thread {
class context {
  manual_event_loop loop_;
  std::thread thread_;

public:
  context() : loop_(), thread_([this] { loop_.run(); }) {}

  ~context() {
    loop_.stop();
    thread_.join();
  }

  auto get_scheduler() noexcept {
    return loop_.get_scheduler();
  }

  std::thread::id get_thread_id() const noexcept {
    return thread_.get_id();
  }
};
} // namespace _single_thread

using single_thread_context = _single_thread::context;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
