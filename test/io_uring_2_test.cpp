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

#if !UNIFEX_NO_LIBURING
#  if !UNIFEX_NO_COROUTINES

#    include <unifex/linux/io_uring_context.hpp>

#    include <unifex/finally.hpp>
#    include <unifex/inplace_stop_token.hpp>
#    include <unifex/never.hpp>
#    include <unifex/scheduler_concepts.hpp>
#    include <unifex/stop_when.hpp>
#    include <unifex/stream_concepts.hpp>
#    include <unifex/sync_wait.hpp>
#    include <unifex/task.hpp>
#    include <unifex/then.hpp>
#    include <unifex/when_all.hpp>

#    include <chrono>
#    include <cstdio>
#    include <fcntl.h>
#    include <string>
#    include <thread>

#    include <gtest/gtest.h>
using namespace unifex;
using namespace unifex::linuxos;
namespace {
constexpr std::chrono::milliseconds stopAfter{42};
const char* fdPath = "/proc/self/fd/";

struct IOUringTest : testing::Test {
  void SetUp() override {
    ASSERT_NE(pipe(pipes_), -1) << "unable to create pipe";
    close_ = true;
  }

  ~IOUringTest() {
    if (close_) {
      close(pipes_[0]);
      close(pipes_[1]);
    }
    stopSource_.request_stop();
    t_.join();
  }

private:
  bool close_{false};

protected:
  int pipes_[2];
  io_uring_context ctx_;
  inplace_stop_source stopSource_;
  std::thread t_{[&] {
    ctx_.run(stopSource_.get_token());
  }};

  task<void> accept(io_uring_context::scheduler sched) {
    // open on a random port, will hang forever
    auto stream = open_listening_socket(sched, 0);
    co_await finally(unifex::next(stream), unifex::cleanup(stream));
    ADD_FAILURE() << "should cancel and unroll";
  }
  task<void> read(io_uring_context::scheduler sched) {
    auto in = open_file_read_only(sched, fdPath + std::to_string(pipes_[0]));
    std::array<char, 1024> buffer;
    // will hang forever
    co_await async_read_some_at(
        in, 0, as_writable_bytes(span{buffer.data(), buffer.size()}));
    ADD_FAILURE() << "should cancel and unroll";
  }

  auto bloat() const {
    // pipe is blocking when full (what we want), settings are env. specific
    auto size = fcntl(pipes_[1], F_GETPIPE_SZ);
    EXPECT_GT(size, 0);
    std::printf("Pipe size: %d\n", size);
    return std::string(static_cast<std::size_t>(size), '?');
  }

  task<void> write(io_uring_context::scheduler sched) {
    auto data = bloat();
    const auto buffer = as_bytes(span{data.data(), data.size()});
    auto out = open_file_write_only(sched, fdPath + std::to_string(pipes_[1]));
    // Start 8 concurrent writes to the file at different offsets.
    co_await when_all(
        // Calls the 'async_write_some_at()' CPO on the file object
        // returned from 'open_file_write_only()'.
        async_write_some_at(out, 0, buffer),
        async_write_some_at(out, 1 * buffer.size(), buffer),
        async_write_some_at(out, 2 * buffer.size(), buffer),
        async_write_some_at(out, 3 * buffer.size(), buffer),
        async_write_some_at(out, 4 * buffer.size(), buffer),
        async_write_some_at(out, 5 * buffer.size(), buffer),
        async_write_some_at(out, 6 * buffer.size(), buffer),
        async_write_some_at(out, 7 * buffer.size(), buffer));
    ADD_FAILURE() << "should cancel and unroll";
  }
};

task<void>
stopTrigger(std::chrono::milliseconds ms, io_uring_context::scheduler sched) {
  co_await stop_when(
      schedule_at(sched, now(sched) + ms) |
          then([ms] { std::printf("Timeout after %ldms\n", ms.count()); }),
      never_sender());
}
}  // namespace

TEST_F(IOUringTest, AsyncReadCancel) {
  auto scheduler = ctx_.get_scheduler();
  // cancel the read from *nix pipe
  sync_wait(stop_when(read(scheduler), stopTrigger(stopAfter, scheduler)));
}

TEST_F(IOUringTest, AsyncWriteCancel) {
  auto scheduler = ctx_.get_scheduler();
  // cancel the write into *nix pipe
  sync_wait(stop_when(write(scheduler), stopTrigger(stopAfter, scheduler)));
}

TEST_F(IOUringTest, AcceptCancel) {
  auto scheduler = ctx_.get_scheduler();
  // cancel the accept stream
  sync_wait(stop_when(accept(scheduler), stopTrigger(stopAfter, scheduler)));
}

#  endif  // UNIFEX_NO_LIBURING
#endif    // UNIFEX_NO_LIBURING
