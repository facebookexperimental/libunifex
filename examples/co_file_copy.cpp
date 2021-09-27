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

#undef UNIFEX_NO_COROUTINES
#define UNIFEX_NO_COROUTINES false
#undef UNIFEX_NO_COROUTINES_HEADER
#define UNIFEX_COROUTINES_HEADER <coroutine>
#undef UNIFEX_NO_COROUTINES_NAMESPACE
#define UNIFEX_COROUTINES_NAMESPACE std

//#if !UNIFEX_NO_COROUTINES

#if !UNIFEX_NO_LIBURING

#include <unifex/scheduler_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/async_manual_reset_event.hpp>
#include <unifex/linux/io_uring_context.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/async_scope.hpp>
#include <unifex/defer.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/let_error.hpp>
#include <unifex/let_done.hpp>
#include <unifex/sequence.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/finally.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/then.hpp>
#include <unifex/task.hpp>
#include <unifex/just.hpp>
#include <unifex/just_from.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/repeat_effect_until.hpp>

#include <cstdio>

#include <chrono>
#include <iostream>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <filesystem>

using namespace unifex;
using namespace unifex::linuxos;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace std::string_view_literals;
namespace fs = std::filesystem;

inline constexpr size_t bufferSize = 64000;
inline constexpr int highInFlightMarker = 500;
inline constexpr int lowInFlightMarker = 400;

inline constexpr auto error_as_exception_ptr = let_error([](auto e){
    using error_t = decltype(e);
    std::exception_ptr ep;
    if constexpr (same_as<error_t, std::exception_ptr>) {
      try {std::rethrow_exception(e);} catch(const std::exception& ex) {
        printf("EXCEPTIONPTR: '%s'\n", ex.what()); fflush(stdout);
      }
      ep = e;
    } else if constexpr (same_as<error_t, std::error_code>) {
      printf("ERROR: '%s'\n", e.message().c_str()); fflush(stdout);
      ep = std::make_exception_ptr(std::system_error(e));
    } else {
      printf("UNKNOWN:\n"); fflush(stdout);
      ep = std::make_exception_ptr((error_t&&)e);
    }
    return just_error(ep);
  });

// This could be made generic across any scheduler that supports the
// async_write_only_file() CPO.

using writable_file_t = callable_result_t<tag_t<open_file_write_only>, io_uring_context::scheduler, std::string>;
task<int> write(writable_file_t& to, int index, span<char> pending) {
  int result = 0;
  while(pending.size() != 0) {
    auto bytesWritten = co_await (async_write_some_at(to, index, as_bytes(pending)) 
    | error_as_exception_ptr);

    index += bytesWritten;
    result += bytesWritten;
    pending = span{pending.begin() + bytesWritten, pending.size() - bytesWritten};
  }
  co_return result;
}

using readable_file_t = callable_result_t<tag_t<open_file_read_only>, io_uring_context::scheduler, std::string>;
using writable_bytes_t = decltype(as_writable_bytes(span<char>{}));
task<int> read_some_at(readable_file_t& from, int index, writable_bytes_t bytes) {
  co_return co_await (async_read_some_at(from, index, bytes)
    | error_as_exception_ptr);
}

task<void> copy_file(io_uring_context::scheduler s, std::string from, std::string to) {
  auto file_from = open_file_read_only(s, from);
  auto file_to = open_file_write_only(s, to); 

  int index = 0; 
  std::vector<char> buffer;

  buffer.resize(bufferSize);
  buffer.resize(buffer.capacity());
  const auto writableBytes = as_writable_bytes(span{buffer.data(), buffer.size()});

  while(!buffer.empty()){
    auto bytesRead = co_await read_some_at(file_from, index, writableBytes);
    if(bytesRead == 0) {
      break;
    }
    auto bytesWritten = co_await write(file_to, index, span{buffer.data(), size_t(bytesRead)});
    index += bytesWritten;
  }
}

#if 0
int main(int argc, char* argv[]) {
  if (argc < 3)
    std::cout << "usage: vcopy what where\n";

  io_uring_context ctx;
  inplace_stop_source stopSource;
  std::thread t{[&] { ctx.run(stopSource.get_token()); }};
  scope_guard stopOnExit = [&]() noexcept {
    stopSource.request_stop();
    t.join();
  };

  auto scheduler = ctx.get_scheduler();

  sync_wait(copy_file(scheduler, argv[1], argv[2]));
}
#endif

task<void> copy_files(
  io_uring_context::scheduler s, 
  const fs::path& from, 
  const fs::path& to) 
{
  unifex::async_scope scope;
  std::atomic<int> pending{0}; 
  unifex::async_manual_reset_event drain;

  drain.set();

  std::exception_ptr ep;
  try {
    for(auto& entry : fs::recursive_directory_iterator(from)) {
      if (entry.is_directory()) {
        // skip this item 
        continue;
      }

      if(++pending >= highInFlightMarker && drain.ready()) {
        // wait for some files to complete
        drain.reset();
      }

      co_await (drain.async_wait() | with_query_value(get_scheduler, s));

      const auto& p = entry.path();
      // Create path in target, if not existing.
      const auto relativeSrc = fs::relative(p, from);
      const auto targetParentPath = to / relativeSrc.parent_path();
      const auto targetParentFile = targetParentPath / p.filename();

      scope.spawn([](
        // use parameters instead of captures because async_scope
        // will run this after this scope has unwound 
        io_uring_context::scheduler s, 
        std::atomic<int>& pending, 
        unifex::async_manual_reset_event& drain,
        std::string p,
        std::string targetParentPath,
        std::string targetParentFile
      )->task<void>{

        try {
          fs::create_directories(targetParentPath);

          // Copy to the targetParentPath which we just created.
          co_await copy_file(s, p, targetParentFile);

          printf("%d: %s -> %s\n", pending.load(), p.c_str(), targetParentFile.c_str());
        } catch (const std::exception& ex) {
          printf("EXCEPTION: '%s' %d: %s -> %s\n", ex.what(), pending.load(), p.c_str(), targetParentFile.c_str());
          throw;
        } catch(...) {
          printf("UNKNOWN EXCEPTION: %d: %s -> %s\n", pending.load(), p.c_str(), targetParentFile.c_str());
          throw;
        }
        fflush(stdout);

        if (--pending <= lowInFlightMarker && !drain.ready()) { 
          // resume file iteration
          drain.set();
        }
      }(s, pending, drain, p.string(), targetParentPath.string(), targetParentFile.string()));
    }
  } catch(...) { ep = std::current_exception(); }
  // can't co_await in  catch block or destructor. so save any exception
  co_await (scope.complete() | with_query_value(get_scheduler, s));
  // rethrow if an exception was caught
  if (!!ep) {std::rethrow_exception(ep);}
}


auto copy_files(const fs::path& from, const fs::path& to) noexcept
{
    for (const auto& dirEntry : fs::recursive_directory_iterator(from))
    {
        if (dirEntry.is_directory()) { continue; }

        const auto& p = dirEntry.path();
        // Create path in target, if not existing.
        const auto relativeSrc = fs::relative(p, from);
        const auto targetParentPath = to / relativeSrc.parent_path();
        const auto targetParentFile = targetParentPath / p.filename();
 
        fs::create_directories(targetParentPath);

        // Copy to the targetParentPath which we just created.
        try {
          copy_file(p, targetParentFile, fs::copy_options::overwrite_existing);
          printf("%s -> %s\n", p.c_str(), targetParentFile.c_str());
        } catch(const std::exception& ex) {
          printf("EXCEPTION: '%s' %s -> %s\n", ex.what(), p.c_str(), targetParentFile.c_str());
        }
    }
}

struct stop_running {
  inplace_stop_source& stopSource_;
  friend void tag_invoke(unifex::tag_t<unifex::set_value>, stop_running&& self, auto&&...) {
    self.stopSource_.request_stop();
  }
  template<typename Error>
  friend void tag_invoke(unifex::tag_t<unifex::set_error>, stop_running&& self, Error&&) noexcept {
    self.stopSource_.request_stop();
  }
  friend void tag_invoke(unifex::tag_t<unifex::set_done>, stop_running&& self) noexcept {
    self.stopSource_.request_stop();
  }
};

int main(int argc, char* argv[]) {
  fs::path from;
  fs::path to;
  bool use_std_copy = false;

  int position = 0;
  std::vector<std::string_view> args(argv+1, argv+argc);
  for (auto arg : args) {
    if (arg.find("usestd"sv) == 0) {
      use_std_copy = true;
    } else {
      if (position == 0) {
        printf("from: -> %s\n", arg.data());
        from = arg;
      } else if (position == 1){
        printf("to: -> %s\n", arg.data());
        to = arg;
      } else {
        printf("error: too many positional arguments!");
        return -1;
      }
      ++position;
    }
  }

  io_uring_context ctx;
  inplace_stop_source stopSource;

  auto scheduler = ctx.get_scheduler();

  try {
    using double_sec = duration<double>;
    auto start = steady_clock::now();
    auto finish = steady_clock::now();
    if (use_std_copy) {
      start = steady_clock::now();
      copy_files(from, to);
      finish = steady_clock::now();
      printf("std filesystem: Copied all the files in %6.6f seconds\n",
              duration_cast<double_sec>(finish-start).count());
      fflush(stdout); 
    } else {
      auto op = unifex::connect(
        sequence(
          scheduler.schedule(),
          just_from([&] { 
            std::printf("copy file\n"); 
            fflush(stdout); 
            start = steady_clock::now();
          }),
          copy_files(scheduler, from, to),
          just_from([&] { 
            finish = steady_clock::now();
            std::printf("copy completed\n");
            fflush(stdout); 
          })), stop_running{stopSource});
      unifex::start(op);

      printf("running...\n");
      ctx.run(stopSource.get_token());

      printf("uring: Copied all the files in %6.6f seconds\n",
              duration_cast<double_sec>(finish-start).count());
      fflush(stdout); 
    }
  } catch (const std::exception& ex) {
    std::printf("error: %s\n", ex.what());
    fflush(stdout); 
  }

  return 0;
}

#else // UNIFEX_NO_LIBURING

#include <cstdio>
int main() {
  printf("liburing support not found\n");
  return 0;
}

#endif // UNIFEX_NO_LIBURING

// #else // UNIFEX_NO_COROUTINES

// #include <cstdio>
// int main() {
//   printf("coroutine support not found\n");
//   return 0;
// }

// #endif // UNIFEX_NO_COROUTINES
