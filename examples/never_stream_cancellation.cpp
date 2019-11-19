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
#include <unifex/for_each.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/never.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/scope_guard.hpp>

#include <chrono>
#include <cstdio>
#include <optional>
#include <thread>

using namespace std::literals::chrono_literals;
using namespace unifex;

int main() {
  inplace_stop_source stopSource;

  std::thread t{[&] {
    std::this_thread::sleep_for(100ms);

    std::printf("requesting stop\n");
    std::fflush(stdout);

    stopSource.request_stop();

    std::printf("request_stop() returned\n");
    std::fflush(stdout);
  }};
  scope_guard joinThread = [&]() noexcept { t.join(); };

  std::optional<unit> result = sync_wait(
      for_each(
          never_stream{},
          [](auto) {
            std::printf("got value");
            std::fflush(stdout);
          }),
      stopSource.get_token());

  std::printf("completed with %s\n", result ? "unit" : "nullopt");

  return 0;
}
