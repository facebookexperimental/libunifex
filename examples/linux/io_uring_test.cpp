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

#include <unifex/config.hpp>

#if !UNIFEX_NO_LIBURING

#include <unifex/inplace_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/let_with.hpp>
#include <unifex/linux/io_uring_context.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sequence.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/when_all.hpp>
#include <unifex/with_query_value.hpp>
#include <unifex/just_with.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace unifex;
using namespace unifex::linuxos;
using namespace std::chrono_literals;

template<typename S>
auto discard_value(S&& s) {
  return transform((S&&)s, [](auto&&...) noexcept {});
}

static constexpr unsigned char data[6] = {'h', 'e', 'l', 'l', 'o', '\n'};

// This could be made generic across any scheduler that supports the
// async_write_only_file() CPO.
auto write_new_file(io_uring_context::scheduler s, const char* path) {
  return let_with(
      [s, path]() {
        // Call the 'open_file_write_only' CPO with the scheduler.
        // This will return a file object that satisfies an
        // async-write-file concept.
        return open_file_write_only(s, path);
      },
      [](io_uring_context::async_write_only_file& file) {
        const auto buffer = as_bytes(span{data});
        // Start 8 concurrent writes to the file at different offsets.
        return discard_value(when_all(
            // Calls the 'async_write_some_at()' CPO on the file object
            // returned from 'open_file_write_only()'.
            async_write_some_at(file, 0, buffer),
            async_write_some_at(file, 1 * buffer.size(), buffer),
            async_write_some_at(file, 2 * buffer.size(), buffer),
            async_write_some_at(file, 3 * buffer.size(), buffer),
            async_write_some_at(file, 4 * buffer.size(), buffer),
            async_write_some_at(file, 5 * buffer.size(), buffer),
            async_write_some_at(file, 6 * buffer.size(), buffer),
            async_write_some_at(file, 7 * buffer.size(), buffer)));
      });
}

auto read_file(io_uring_context::scheduler s, const char* path) {
  return let_with(
      [s, path]() { return open_file_read_only(s, path); },
      [buffer = std::vector<char>{}](auto& file) mutable {
        buffer.resize(100);
        return transform(
            async_read_some_at(
                file,
                0,
                as_writable_bytes(span{buffer.data(), buffer.size() - 1})),
            [&](ssize_t bytesRead) {
              std::printf("read %zi bytes\n", bytesRead);
              buffer[bytesRead] = '\0';
              std::printf("contents: %s\n", buffer.data());
            });
      });
}

int main() {
  io_uring_context ctx;

  inplace_stop_source stopSource;
  std::thread t{[&] { ctx.run(stopSource.get_token()); }};
  scope_guard stopOnExit = [&]() noexcept {
    stopSource.request_stop();
    t.join();
  };

  auto scheduler = ctx.get_scheduler();

  try {
    {
      auto startTime = std::chrono::steady_clock::now();
      inplace_stop_source timerStopSource;
      sync_wait(
        with_query_value(
          when_all(
              transform(
                  schedule_at(scheduler, now(scheduler) + 1s),
                  []() { std::printf("timer 1 completed (1s)\n"); }),
              transform(
                  schedule_at(scheduler, now(scheduler) + 2s),
                  []() { std::printf("timer 2 completed (2s)\n"); }),
              transform(
                  schedule_at(scheduler, now(scheduler) + 1500ms),
                  [&]() {
                    std::printf("timer 3 completed (1.5s) cancelling\n");
                    timerStopSource.request_stop();
                  })),
          get_stop_token,
          timerStopSource.get_token()));
      auto endTime = std::chrono::steady_clock::now();

      std::printf(
          "completed in %i ms\n",
          (int)std::chrono::duration_cast<std::chrono::milliseconds>(
              endTime - startTime)
              .count());
    }

    sync_wait(sequence(
        just_with([] { std::printf("writing file\n"); }),
        write_new_file(scheduler, "test.txt"),
        just_with([] { std::printf("write completed, waiting 1s\n"); }),
        transform(
            schedule_at(scheduler, now(scheduler) + 1s),
            []() { std::printf("timer 1 completed (1s)\n"); }),
        just_with([] { std::printf("reading file concurrently\n"); }),
        when_all(
            read_file(scheduler, "test.txt"),
            read_file(scheduler, "test.txt"))));
  } catch (const std::exception& ex) {
    std::printf("error: %s\n", ex.what());
  }

  return 0;
}

#else // UNIFEX_NO_LIBURING

#include <cstdio>
int main() {
  printf("liburing support not found\n");
  return 0;
}

#endif // UNIFEX_NO_LIBURING
