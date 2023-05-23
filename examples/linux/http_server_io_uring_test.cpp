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

#if !UNIFEX_NO_LIBURING && !UNIFEX_NO_COROUTINES
#  include <unifex/for_each.hpp>
#  include <unifex/linux/io_uring_context.hpp>
#  include <unifex/on.hpp>
#  include <unifex/scheduler_concepts.hpp>
#  include <unifex/spawn_detached.hpp>
#  include <unifex/stop_when.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>
#  include <unifex/then.hpp>
#  include <unifex/v2/async_scope.hpp>
#  include <unifex/via.hpp>

#  include <cstdio>
#  include <cstdlib>

using namespace unifex;
using namespace unifex::linuxos;
using namespace std::string_view_literals;
namespace {
static constexpr port_t port = 8080;
static constexpr std::size_t buffer_size = 1024;
// payloads
static constexpr auto divider = "\r\n\r\n"sv;
static constexpr auto not_allowed = "HTTP/1.1 405 Method Not Allowed\r\n\r\n"sv;
static constexpr std::string_view index =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n\r\n"
    "<!DOCTYPE html>\r\n"
    "<html><head>\r\n"
    "<title>coroutine based http:// server demo</title>\r\n"
    "<link rel=\"icon\" type=\"image/x-icon\" "
    "href=\"data:image/"
    "x-icon;base64,"
    "AAABAAEAEBACAAAAAACwAAAAFgAAACgAAAAQAAAAIAAAAAEAAQAAAAAAQAAAAAAAAAAAAAAAAg"
    "AAAAAAAAAAAAAAD///AP//AAD//wAA778AALffAAD77wAAvfcAAP77AAD//wAA//"
    "8AAMzDAAC7fwAAu38AAMz/AAD//wAA//8AAP//"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAA\">"
    "</head><body>Hello from <code>unifex::</code></body></html>\r\n";

enum class Method {
  GET,
  OTHER,
};

struct Request {
  Method method{Method::OTHER};
  std::string headers;
  std::string body;
};

task<Request>
parse_request(io_uring_context::async_read_write_file& readWriteFile) {
  std::array<char, buffer_size> buffer;
  std::string req;
  Request request;
  while (auto read = co_await async_read_some_at(
             readWriteFile,
             0,
             as_writable_bytes(span{buffer.data(), buffer.size()}))) {
    if (read < 0) {
      break;
    }
    req.append(buffer.data(), read);
    if (req.size() < 3) {
      // too small, keep going
      continue;
    }
    if (req.starts_with("GET")) {
      request.method = Method::GET;
    } else {
      // not supported
      break;
    }
    if (auto idx = req.find(divider); idx != std::string::npos) {
      request.headers = req.substr(0, idx);
      break;
    }
    // protect from infite request
    if (req.size() > 8 * buffer.size()) {
      std::printf("req too big=%ld\n", req.size());
      request.method = Method::OTHER;
      break;
    }
  }
  if (req.size() == 0) {
    // not a valid http, cancel
    co_await just_done();  // TODO co_yield stop()
  }
  co_return std::move(request);
}

task<void> handle(io_uring_context::async_read_write_file readWriteFile) {
  auto req = co_await parse_request(readWriteFile);
  if (req.method != Method::GET) {
    auto rsp = not_allowed;
    std::printf("writing=%s\n", rsp.data());
    co_await async_write_some_at(
        readWriteFile, 0, as_bytes(span{rsp.data(), rsp.size()}));
  } else if (req.body.empty()) {
    auto rsp = index;
    std::printf("writing=%s\n", rsp.data());
    co_await async_write_some_at(
        readWriteFile, 0, as_bytes(span{rsp.data(), rsp.size()}));
  } else {
    std::printf("unhandled request\n");
    co_await just_done();
  }
}

task<void> run(io_uring_context::scheduler sched) {
  // mangle bulk_transform to support Sender returning []{}
  v2::async_scope requests;
  auto mainThread = co_await current_scheduler();
  std::printf("opening port=%d, hit 'q' to stop\n", port);
  co_await for_each(
      open_listening_socket(sched, port),
      [&mainThread, &requests](auto readWriteFile) {
        spawn_detached(
            on(mainThread, handle(std::move(readWriteFile))), requests);
      });
  co_await requests.join();
}

task<void> quit(io_uring_context::scheduler sched) {
  auto in = open_file_read_only(sched, "/dev/stdin");
  std::array<char, buffer_size> buffer;
  while (auto read = co_await async_read_some_at(
             in, 0, as_writable_bytes(span{buffer.data(), buffer.size()}))) {
    if (read > 0 && buffer[0] == 'q') {
      std::printf("quit requested\n");
      co_return;
    }
  }
}

task<void>
stopTrigger(std::chrono::milliseconds ms, io_uring_context::scheduler sched) {
  if (ms.count() > 0) {
    co_await stop_when(
        schedule_at(sched, now(sched) + ms) |
            then([ms] { std::printf("Timeout after %ldms\n", ms.count()); }),
        quit(sched));
  } else {
    co_await quit(sched);
  }
}
}  // namespace

int main(int argc, const char** argv) {
  auto usage = [&]() noexcept {
    std::printf(
        "usage: %s [TIMEOUT_MS (quit after TIMEOUT_MS, default 1000, 0 means "
        "infinity)]\n",
        argv[0]);
    return 1;
  };
  if (argc > 2) {
    return usage();
  }
  std::uint64_t timeoutMs = 1000;
  if (argc == 2) {
    const char* start = argv[1];
    char* end = nullptr;
    auto ms = std::strtoul(start, &end, 10);
    if (end == argv[1] || errno) {
      return usage();
    }
    timeoutMs = ms;
  }
  io_uring_context ctx;

  inplace_stop_source stopSource;
  std::thread t{[&] {
    ctx.run(stopSource.get_token());
  }};
  scope_guard stopOnExit = [&]() noexcept {
    stopSource.request_stop();
    t.join();
  };
  sync_wait(stop_when(
      run(ctx.get_scheduler()),
      stopTrigger(std::chrono::milliseconds{timeoutMs}, ctx.get_scheduler())));
  return 0;
}

#else  // UNIFEX_NO_LIBURING

#  include <cstdio>
int main() {
  printf("liburing / coroutines support not found\n");
  return 0;
}

#endif  // UNIFEX_NO_LIBURING
