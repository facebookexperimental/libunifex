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

#include <unifex/inplace_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/let.hpp>
#include <unifex/linux/io_epoll_context.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sequence.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/when_all.hpp>
#include <unifex/with_query_value.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace unifex;
using namespace unifex::linuxos;
using namespace std::chrono_literals;

template <typename F>
auto lazy(F&& f) {
  return transform(just(), (F &&) f);
}

int main() {
  io_epoll_context ctx;

  inplace_stop_source stopSource;
  std::thread t{[&] { ctx.run(stopSource.get_token()); }};
  scope_guard stopOnExit = [&]() noexcept {
    stopSource.request_stop();
    t.join();
  };

  auto scheduler = ctx.get_scheduler();
  try {
    {
      auto start = std::chrono::steady_clock::now();
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
      auto end = std::chrono::steady_clock::now();

      std::printf(
          "completed in %i ms\n",
          (int)std::chrono::duration_cast<std::chrono::milliseconds>(
              end - start)
              .count());
    }
  } catch (const std::exception& ex) {
    std::printf("error: %s\n", ex.what());
  }
  return 0;
}

#else // !UNIFEX_NO_EPOLL
#include <cstdio>
int main() {
  printf("epoll support not found\n");
}
#endif // !UNIFEX_NO_EPOLL
