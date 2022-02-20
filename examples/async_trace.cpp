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
#include <unifex/async_trace.hpp>

#include <unifex/sequence.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>

#include <unifex/config.hpp>
#include <unifex/just.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/then.hpp>
#include <unifex/finally.hpp>
#include <unifex/when_all.hpp>
#include <unifex/defer.hpp>

#if !UNIFEX_NO_COROUTINES
#include <unifex/task.hpp>
#endif

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

auto dump_async_trace(std::string tag = {}) {
  return then(
      async_trace_sender{},
      [tag = std::move(tag)](const std::vector<async_trace_entry> &entries) {
        std::cout << "Async Trace (" << tag << "):\n";
        for (auto &entry : entries) {
          std::cout << " " << entry.depth << " [-> " << entry.parentIndex
                    << "]: " << entry.continuation.type().name() << " @ 0x";
          std::cout.setf(std::ios::hex, std::ios::basefield);
          std::cout << entry.continuation.address();
          std::cout.unsetf(std::ios::hex);
          std::cout << "\n";
        }
      });
}

template <typename Sender>
auto dump_async_trace_on_start(Sender &&sender, std::string tag = {}) {
  return unifex::sequence(dump_async_trace(std::move(tag)), (Sender &&) sender);
}

template <typename Sender>
auto dump_async_trace_on_completion(Sender &&sender, std::string tag = {}) {
  return unifex::finally((Sender &&) sender,
                          dump_async_trace(std::move(tag)));
}

#if !UNIFEX_NO_COROUTINES
UNIFEX_CLANG_DISABLE_OPTIMIZATION
task<int> dump_async_trace_in_coroutine() {
  co_await dump_async_trace("coroutine");
  co_return 42;
}
#endif

int main() {
  timed_single_thread_context context;

  auto startTime = steady_clock::now();

  sync_wait(
    then(
      when_all(
        then(
          dump_async_trace_on_start(
            schedule_after(context.get_scheduler(), 100ms), "part1"),
          [=]() {
            auto time = steady_clock::now() - startTime;
            auto timeMs = duration_cast<milliseconds>(time).count();
            std::cout << "part1 finished - [" << timeMs << "]\n";
            return time;
          }),
        then(
          dump_async_trace_on_completion(
            schedule_after(context.get_scheduler(), 200ms), "part2"),
          [=]() {
            auto time = steady_clock::now() - startTime;
            auto timeMs = duration_cast<milliseconds>(time).count();
            std::cout << "part2 finished - [" << timeMs << "]\n";
            return time;
          }),
#if !UNIFEX_NO_COROUTINES
        dump_async_trace_in_coroutine()
#else
        just(42)
#endif // UNIFEX_NO_COROUTINES
      ),
      [](auto &&a, auto &&b, auto &&c) {
        std::cout
            << "when_all finished - ["
            << duration_cast<milliseconds>(std::get<0>(std::get<0>(a))).count()
            << ", "
            << duration_cast<milliseconds>(std::get<0>(std::get<0>(b))).count()
            << ", "
            << std::get<0>(std::get<0>(c))
            << "]\n";
      }));

  std::cout << "all done\n";

  return 0;
}
