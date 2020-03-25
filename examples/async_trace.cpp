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
#include <unifex/async_trace.hpp>

#include <unifex/sequence.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>

#include <unifex/config.hpp>
#include <unifex/just.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/transform.hpp>
#include <unifex/finally.hpp>
#include <unifex/when_all.hpp>

#if !UNIFEX_NO_COROUTINES
#include <unifex/awaitable_sender.hpp>
#include <unifex/sender_awaitable.hpp>
#include <unifex/task.hpp>
#endif

#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <string>
#include <vector>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

auto dump_async_trace(std::string tag = {}) {
  return transform(
      async_trace_sender{},
      [tag = std::move(tag)](const std::vector<async_trace_entry> &entries) {
        std::stringstream s;
        s << "Async Trace (" << tag << "):\n";
        for (auto &entry : entries) {
          s << " " << entry.depth << " [-> " << entry.parentIndex
                    << "]: " << entry.continuation.type().name() << " @ 0x";
          s.setf(std::ios::hex, std::ios::basefield);
          s << entry.continuation.address();
          s.unsetf(std::ios::hex);
          s << "\n";
        }
        std::puts(s.str().c_str());
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

int main() {
  timed_single_thread_context context;

  auto start = steady_clock::now();

  sync_wait(transform(
      when_all(
          transform(
              dump_async_trace_on_start(
                  schedule_after(context.get_scheduler(), 100ms), "part1"),
              [=]() {
                auto time = steady_clock::now() - start;
                auto timeMs = duration_cast<milliseconds>(time).count();
                std::stringstream s;
                s << "part1 finished - [" << timeMs << "]";
                std::puts(s.str().c_str());
                return time;
              }),
          transform(
              dump_async_trace_on_completion(
                  schedule_after(context.get_scheduler(), 200ms), "part2"),
              [=]() {
                auto time = steady_clock::now() - start;
                auto timeMs = duration_cast<milliseconds>(time).count();
                std::stringstream s;
                s << "part2 finished - [" << timeMs << "]\n";
                std::puts(s.str().c_str());
                return time;
              }),
#if !UNIFEX_NO_COROUTINES
          awaitable_sender(
            []() -> task<int> {
              co_await dump_async_trace("coroutine");
              co_return 42;
            }()
          )
#else
          just(42)
#endif // UNIFEX_NO_COROUTINES
          ),
      [](auto &&a, auto &&b, auto &&c) {
        std::stringstream s;
        s
            << "when_all finished - ["
            << duration_cast<milliseconds>(std::get<0>(std::get<0>(a))).count()
            << ", "
            << duration_cast<milliseconds>(std::get<0>(std::get<0>(b))).count()
            << ", "
            << std::get<0>(std::get<0>(c))
            << "]\n";
        std::puts(s.str().c_str());
      }));

  std::puts("all done");

  return 0;
}
