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

#include <unifex/async_mutex.hpp>
#include <unifex/sync_wait.hpp>

#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES

#include <unifex/awaitable_sender.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_awaitable.hpp>
#include <unifex/task.hpp>
#include <unifex/when_all.hpp>
#include <unifex/single_thread_context.hpp>

#include <cstdio>

using namespace unifex;

int main() {
  async_mutex mutex;

  int sharedState = 0;

  auto makeTask = [&](manual_event_loop::scheduler scheduler) -> task<int> {
    for (int i = 0; i < 100'000; ++i) {
      co_await mutex.async_lock();
      co_await schedule(scheduler);
      ++sharedState;
      mutex.unlock();
    }
    co_return 0;
  };

  single_thread_context ctx1;
  single_thread_context ctx2;

  sync_wait(when_all(awaitable_sender(makeTask(ctx1.get_scheduler())),
                     awaitable_sender(makeTask(ctx2.get_scheduler()))));

  if (sharedState != 200'000) {
    std::printf("error: incorrect result %i, expected 2000000\n", sharedState);
    return 1;
  }

  return 0;
}

#else // UNIFEX_NO_COROUTINES

#include <cstdio>

int main() {
    // Very simple usage of async_mutex.

    unifex::async_mutex m;
    unifex::sync_wait(m.async_lock());
    m.unlock();
    
    return 0;
}

#endif // UNIFEX_NO_COROUTINES
