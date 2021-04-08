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
    auto this_file = open_file_read_only(sched, filesystem::path{__FILE__});
    auto output_file = open_file_write_only(
        sched, filesystem::path{"file_coroutine_test_copy.cpp"});
    std::array<std::byte, 32> buffer{};
    size_t offset = 0;
    while (size_t read_bytes = co_await async_read_some_at(
               this_file, offset, as_writable_bytes(span{buffer}))) {
      co_await async_write_some_at(
          output_file, offset, as_bytes(span{buffer.data(), read_bytes}));
      offset += read_bytes;
    }
    std::printf("copied %zu bytes\n", offset);
  }());
  return 0;
}
#else
#  include <cstdio>
int main() {
  printf("neither io_ring or coroutine support found\n");
  return 0;
}
#endif
