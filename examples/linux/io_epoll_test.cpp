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

// writes starting!
// warmup completed!
// benchmark completed!
// completed in 10000 ms, 10000110689ns, 7396446ops
// stats - 739636reads, 1352ns-per-op, 739ops-per-ms
// writes stopped!

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
#include <unifex/repeat_effect_until.hpp>
#include <unifex/typed_via.hpp>
#include <unifex/with_query_value.hpp>
#include <unifex/transform_done.hpp>

#include <iostream>
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

template <typename F>
auto defer(F&& f) {
  return let(just(), (F&&)f);
}

template <typename S>
auto discard(S&& s) {
  return transform((S&&)s, [](auto&&...){});
}

//! Seconds to warmup the benchmark
static constexpr int WARMUP_DURATION = 3;

//! Seconds to run the benchmark
static constexpr int BENCHMARK_DURATION = 10;

static constexpr unsigned char data[6] = {'h', 'e', 'l', 'l', 'o', '\n'};

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
          timerStopSource.get_token());
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

  auto pipe = open_pipe(scheduler);

  inplace_stop_source stopWarmup;
  inplace_stop_source stopRead;
  inplace_stop_source stopWrite;
  auto buffer = std::vector<char>{};
  buffer.resize(1);
  auto offset = 0;
  auto reps = 0;
  const auto databuffer = as_bytes(span{data});
  auto pipe_bench = [&, &rPipe = std::get<0>(pipe)](int seconds, auto& stopSource) {
    return transform_done(
      with_query_value(
        discard(
          when_all(
            transform(
              defer(
                [&, seconds](){
                  return schedule_at(scheduler, now(scheduler) + std::chrono::seconds(seconds));
                }),
              [&]{
                  stopSource.request_stop();
              }),
              defer(
                [&](){
                  return typed_via(
                    repeat_effect(
                      transform(
                        discard(
                          async_read_some(rPipe, as_writable_bytes(span{buffer.data() + 0, 1}))),
                        [&]{
                          assert(data[(reps + offset)%sizeof(data)] == buffer[0]);
                          ++reps;
                        })), 
                    scheduler);
                }))),
        get_stop_token, stopSource.get_token()),
      []{return just();});
  };
  auto start = std::chrono::high_resolution_clock::now();
  auto end = std::chrono::high_resolution_clock::now();
  auto& wPipe = std::get<1>(pipe);
  try {
    sync_wait(
      when_all(
        sequence(
          lazy([&]{
            printf("writes starting!\n");
          }),
          transform_done(
            repeat_effect(
              defer(
                [&](){
                  return typed_via(
                    discard(async_write_some(wPipe, databuffer)), 
                    scheduler);
                })),
            []{return just();}),
          lazy([&]{
            printf("writes stopped!\n");
          })),
        sequence(
          pipe_bench(WARMUP_DURATION, stopWarmup), // warmup
          lazy([&]{
            // restart reps and keep offset in data
            offset = reps%sizeof(data);
            reps = 0;
            printf("warmup completed!\n");
            // exclude the warmup time
            start = std::chrono::high_resolution_clock::now();
          }),
          pipe_bench(BENCHMARK_DURATION, stopRead),
          lazy([&]{
            end = std::chrono::high_resolution_clock::now();
            printf("benchmark completed!\n");
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end - start)
                    .count();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    end - start)
                    .count();
            double reads = 1000000000.0 * reps / ns;
            std::cout
                << "completed in "
                << ms << " ms, "
                << ns << "ns, " 
                << reps << "ops\n";
            std::cout
                << "stats - "
                << reads << "reads, " 
                << ns/reps << "ns-per-op, " 
                << reps/ms << "ops-per-ms\n";
            stopWrite.request_stop();
          }))),
        stopWrite.get_token());
  } catch (const std::error_code& ec) {
    std::printf("async_read_some error: %s\n", ec.message().c_str());
  } catch (const std::exception& ex) {
    std::printf("async_read_some exception: %s\n", ex.what());
  }
  return 0;
}

#else // !UNIFEX_NO_EPOLL
#include <cstdio>
int main() {
  printf("epoll support not found\n");
}
#endif // !UNIFEX_NO_EPOLL
