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
#include <unifex/delay.hpp>
#include <unifex/for_each.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/typed_via_stream.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace unifex;

int main() {
  using namespace std::chrono;

  timed_single_thread_context context;

  inplace_stop_source stopSource;
  std::thread t{[&] {
    std::this_thread::sleep_for(500ms);
    std::printf("cancelling\n");
    stopSource.request_stop();
  }};
  scope_guard joinThread = [&]() noexcept {
    t.join();
  };

  auto start = steady_clock::now();

  sync_wait(
      cpo::for_each(
          typed_via_stream(
              delay(context.get_scheduler(), 100ms), range_stream{0, 100}),
          [start](int value) {
            auto ms = duration_cast<milliseconds>(steady_clock::now() - start);
            std::printf("[%i ms] %i\n", (int)ms.count(), value);
          }),
      stopSource.get_token());

  return 0;
}
