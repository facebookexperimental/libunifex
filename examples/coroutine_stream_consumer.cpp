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
#include <unifex/config.hpp>

#if !UNIFEX_NO_COROUTINES

#include <unifex/delay.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/single.hpp>
#include <unifex/stop_immediately.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/take_until.hpp>
#include <unifex/task.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/typed_via_stream.hpp>
#include <unifex/let_done.hpp>
#include <unifex/then.hpp>
#include <unifex/just.hpp>

#include <chrono>
#include <cstdio>

using namespace unifex;

template<typename Sender>
auto done_as_optional(Sender&& sender) {
  using value_type = unifex::sender_single_value_result_t<unifex::remove_cvref_t<Sender>>;
  return unifex::let_done(
    unifex::then((Sender&&)sender, [](auto&&... values) {
      return std::optional<value_type>{std::in_place, static_cast<decltype(values)>(values)...};
    }), []() {
      return unifex::just(std::optional<value_type>(std::nullopt));
    });
}

template<typename Sender>
auto done_as_void(Sender&& sender) {
  return let_done((Sender&&)sender, [] { return just(); });
}

int main() {
  using namespace std::chrono;

  timed_single_thread_context context;

  auto makeTask = [&]() -> task<int> {
    auto startTime = steady_clock::now();

    auto s = take_until(
        stop_immediately<int>(
            delay(range_stream{0, 100}, context.get_scheduler(), 50ms)),
        single(schedule_after(context.get_scheduler(), 500ms)));

    int sum = 0;
    while (auto value = co_await done_as_optional(next(s))) {
      auto ms = duration_cast<milliseconds>(steady_clock::now() - startTime);
      std::printf("[%i ms] %i\n", (int)ms.count(), *value);
      std::fflush(stdout);

      sum += *value;
    }

    co_await done_as_void(cleanup(s));

    auto ms = duration_cast<milliseconds>(steady_clock::now() - startTime);
    std::printf("[%i ms] sum = %i\n", (int)ms.count(), sum);
    std::fflush(stdout);

    co_return sum;
  };

  sync_wait(makeTask());

  return 0;
}

#else // UNIFEX_NO_COROUTINES

#include <cstdio>

int main() {
  std::printf(
      "This test only supported for compilers that support coroutines\n");
  return 0;
}

#endif // UNIFEX_NO_COROUTINES
