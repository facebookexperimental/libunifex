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

// requires both coroutine and liburing support (for now)
#if !UNIFEX_NO_COROUTINES and !UNIFEX_NO_LIBURING
#  include <unifex/linux/io_uring_context.hpp>
using io_context = unifex::linuxos::io_uring_context;

#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>

using namespace unifex;

int main() {
  io_context ctx{};
  auto sched = ctx.get_scheduler();
  inplace_stop_source stopSource;
  std::thread t{[&] {
    ctx.run(stopSource.get_token());
  }};
  scope_guard stopOnExit = [&]() noexcept {
    stopSource.request_stop();
    t.join();
  };
  sync_wait([&]() -> task<void> {
    auto file = open_file_write_only(
        sched, filesystem::path{"file_coroutine_test.txt"});
    constexpr char hello[]{"hello\n"};
    size_t offset = 0;
    for (int ii = 0; ii < 42; ++ii) {
      offset += co_await async_write_some_at(
          file, offset, as_bytes(span{hello, sizeof(hello) - 1}));
    }
    std::printf("wrote %zu bytes\n", offset);
  }());
  return 0;
}
#else
#  include <cstdio>
int main() {
  printf("neither io_uring or coroutine support found\n");
  return 0;
}
#endif
