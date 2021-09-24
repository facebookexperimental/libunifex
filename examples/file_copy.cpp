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

#include <unifex/scheduler_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/async_manual_reset_event.hpp>
#include <unifex/linux/io_uring_context.hpp>
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
#include <unifex/just.hpp>
#include <unifex/just_from.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/when_all.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/with_query_value.hpp>

#include <cstdio>

#include <chrono>
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

inline constexpr size_t bufferSize = 65536;
inline constexpr size_t bufferAlign = 512;
inline constexpr int highInFlightMarker = 500;
inline constexpr int lowInFlightMarker = 400;

class timeout_expired : public std::logic_error {
public:
  timeout_expired() : logic_error("timeout expired") {}
  explicit timeout_expired(const std::string& what_arg) : logic_error(what_arg) {}
  explicit timeout_expired(const char* what_arg) : logic_error(what_arg) {}
  timeout_expired(const timeout_expired&) = default;
  timeout_expired& operator=(const timeout_expired&) = default;
};
inline constexpr auto timeout = [](auto&& timeout_at, const auto& sender) { 
  return sender
  | stop_when(schedule_at(timeout_at))
  | let_done([]{return just_error(std::make_exception_ptr(timeout_expired{}));});
};

template<typename SenderFactory>
auto establish_scope(SenderFactory sf) {
  return let_value_with(
    []()->async_scope{return {};},
    [sf](async_scope& scope_) {
      return sequence(
        sf(scope_),
        // wait for all spawns to complete
        scope_.complete())
      | then([](auto&&...){})
      // if we did not complete, then we must cleanup
      | let_error([&] (auto&& e) noexcept {
        return sequence(scope_.cleanup(), just_error((decltype(e)&&)e));
      })
      | let_done([&] () noexcept {
        return sequence(scope_.cleanup(), just_done());
      });
    });
}

template<typename RandomWriter, typename BufferSequence>
auto write_at(RandomWriter& to, typename RandomWriter::offset_t index, BufferSequence bufferSequence) {
  return let_value_with([=]{return std::make_tuple(index, bufferSequence, typename RandomWriter::offset_t{0});},
  [&to_ = to](auto& value) {
    auto& [index_, pending_, bytesWritten_] = value;
    return defer([&]() mutable {
      return async_write_some_at(
        to_, 
        index_, 
        pending_)
      | then([&](ssize_t bytesWritten) {
        index_ += bytesWritten;
        bytesWritten_ += bytesWritten;
        pending_ = as_bytes(span{pending_.begin() + bytesWritten, pending_.size() - bytesWritten});
      });
    })
    | repeat_effect_until([&](){return pending_.size() == 0;})
    | then([&]{return bytesWritten_;});
  });
}

template<typename RandomReader, typename RandomWriter>
auto read_some_write_all(RandomReader& from, RandomWriter& to, span<char> buffer, size_t& index, bool& repeat) {
  return defer([&from_ = from, &to_ = to, buffer_ = buffer, index_ = &index, repeat_ = &repeat]{
    auto writeableBytes = as_writable_bytes(buffer_);

    return async_read_some_at(from_, *index_, writeableBytes)
    | let_value([&](ssize_t bytesRead) {
      span<char> pending{buffer_.begin(), size_t(bytesRead)};

      // signal complete
      if(bytesRead == 0) {*repeat_ = false;}

      return write_at(to_, *index_, as_bytes(pending));
    });
  });
}


// This could be made generic across any scheduler that supports the
// async_write_only_file() CPO.

auto copy_file(io_uring_context::scheduler s, const fs::path& from, const fs::path& to) {
  // introduce new async scope for
  // index_, buffer_, repeat_, from_, to_
  return let_value_with(
    [s, from = from.string(), to = to.string()] {
      // open the from and to files and store the 
      // handles in the scope
      return std::make_tuple(
        open_file_read_only(s, from),
        open_file_write_only(s, to)
      );
    },
    [
      // define state across loop iterations
      index_ = size_t{0}, 
      buffer_ = std::vector<char>{}, 
      repeat_ = true
    ](auto& state) mutable {
      // reference the file handles
      auto& [from_, to_] = state;

      // set buffer size
      buffer_.resize(bufferSize + bufferAlign);
      buffer_.resize(buffer_.capacity());

      // align buffer
      void* lvalueBegin = (void*)buffer_.data();
      size_t lvalueSize = buffer_.size();
      if (nullptr == std::align(bufferAlign, bufferSize, lvalueBegin, lvalueSize)) {
        std::abort();
      }
      span<char> buffer{(char*)lvalueBegin, lvalueSize};

      // read and write loop
      return read_some_write_all(from_, to_, buffer, index_, repeat_)
      // update index_
      | then([&](auto bytesWritten){
        index_ += bytesWritten;
      })
      | repeat_effect_until([&]{return !repeat_;})
      // result is total number of bytes copied
      | then([&]{
        return index_;
      }); 
    });
}

auto limit_open_files(std::atomic<int>& pending, unifex::async_manual_reset_event& drain) {
  return sequence(
    // limit the number of open files
    just_from([&]{
      if(++pending >= highInFlightMarker && drain.ready()) {
        // wait for some files to complete
        drain.reset();
      }
    }),
    // wait if too many files are open
    drain.async_wait());
}

void file_complete(int pend, unifex::async_manual_reset_event& drain) {
  // limit open files
  if (pend <= lowInFlightMarker && !drain.ready()) { 
    // resume file iteration
    drain.set();
  }
}

auto copy_one_file(io_uring_context::scheduler s, const fs::path& from, const fs::path& to, std::atomic<size_t>& bytesCopied, std::atomic<int>& pending, unifex::async_manual_reset_event& drain) {
  return copy_file(s, from, to)
  | then([&, from, to] (auto copiedBytes) noexcept {
    // record and report this successful copy
    auto pend = --pending;
    bytesCopied += copiedBytes;
    printf("%3d: %6ldb from %s to %s\n", pend, copiedBytes, from.c_str(), to.c_str());
    file_complete(pend, drain);
  })
  | let_error([&, from, to](auto e) noexcept {
    // record and report this failure
    auto pend = --pending;
    if constexpr (convertible_to<std::exception_ptr, decltype(e)>) {
      try {std::rethrow_exception(e);} catch(const std::exception& ex) {
        printf("EXCEPTION: '%s' %d: %s -> %s\n", ex.what(), pend, from.c_str(), to.c_str());
      } catch(...) {
        printf("UNKNOWN EXCEPTION: %d: %s -> %s\n", pend, from.c_str(), to.c_str());
      }
    } else if constexpr (convertible_to<std::error_code, decltype(e)>) {
      printf("ERRORCODE: '%s' %d: %s -> %s\n", e.message().c_str(), pend, from.c_str(), to.c_str());
    } else {
      printf("UNKNOWN ERROR: %d: %s -> %s\n", pend, from.c_str(), to.c_str());
    }

    file_complete(pend, drain);
    fflush(stdout);
    // keep going
    return just();
  })
  | let_done([&, from, to] () noexcept {
    // record and report cancellation
    auto pend = --pending;
    printf("CANCELLED: %d: %s -> %s\n", pend, from.c_str(), to.c_str());

    file_complete(pend, drain);
    fflush(stdout);
    // all done
    return just_done();
  });
}

auto queue_file_copy(io_uring_context::scheduler s, const fs::path& from, const fs::path& to, fs::recursive_directory_iterator& entry, unifex::async_scope& scope, std::atomic<size_t>& bytesCopied, std::atomic<int>& pending, unifex::async_manual_reset_event& drain) {
  // queue file copy
  return just_from([&, from_ = from.string(), to_ = to.string()]{
    if (entry == end(entry)) {
      return;
    }
    if (entry->is_directory()) {
      // skip this item 
      --pending;
      return;
    }

    const auto& p = entry->path();
    // Create path in target, if not existing.
    const auto relativeSrc = fs::relative(p, from_);
    const auto targetParentPath = to_ / relativeSrc.parent_path();
    const auto targetParentFile = targetParentPath / p.filename();

    scope.spawn_on(s, sequence(
      // Create the targetParentPath.
      just_from([&, targetParentPath]{
        fs::create_directories(targetParentPath);
      }),
      // Copy to the targetParentPath which we just created.
      copy_one_file(s, p, targetParentFile, bytesCopied, pending, drain)
    ));
  });
}

auto copy_files(
  io_uring_context::scheduler s, 
  const fs::path& from, 
  const fs::path& to) noexcept
{
  // some of this state cannot be moved or copied
  // create a type that allows them to be constructed in place
  using state_t = std::tuple<
    std::atomic<size_t>,
    fs::recursive_directory_iterator, 
    std::atomic<int>, 
    unifex::async_manual_reset_event>;
  // create new async scope for
  // s_, from_, to_, bytesCopied_, entry_, pending_, drain_
  return let_value_with(
    []() -> state_t {return {};}, // store state in scope
    [s_ = s, from_ = from.string(), to_ = to.string()](state_t& state) {
      // reference the state
      auto& [bytesCopied_, entry_, pending_, drain_] = state;

      // initialize the state
      entry_ = fs::recursive_directory_iterator(from_);
      drain_.set();

      // loop through all the directory entries and copy all the files
      return establish_scope([&](unifex::async_scope& scope_) {
        return sequence(
          limit_open_files(pending_, drain_),
          queue_file_copy(s_, from_, to_, entry_, scope_, bytesCopied_, pending_, drain_))
          | repeat_effect_until([&]{return entry_ == end(entry_) || ++entry_ == end(entry_);});
      })
      // the result is the count of bytes that were copied
      | then([&]{return bytesCopied_.load();});
    });
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
          printf("%3d: ?b from %s to %s\n", 0, p.c_str(), targetParentFile.c_str());
        } catch(const std::exception& ex) {
          printf("EXCEPTION: '%s' %s -> %s\n", ex.what(), p.c_str(), targetParentFile.c_str());
          fflush(stdout);
        }
    }
}

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
  std::thread t{[&] { ctx.run(stopSource.get_token()); }};
  scope_guard stopOnExit = [&]() noexcept {
    stopSource.request_stop();
    t.join();
  };

  auto scheduler = ctx.get_scheduler();

  try {
    using double_sec = duration<double>;
    auto start = steady_clock::now();
    auto finish = steady_clock::now();
    if (use_std_copy) {
      start = steady_clock::now();
      copy_files(from, to);
      finish = steady_clock::now();
      printf("copied ?b\n");
      printf("std filesystem: Copied all the files in %6.6f seconds\n",
              duration_cast<double_sec>(finish-start).count());
      fflush(stdout); 
    } else {
      sync_wait(sequence(
          just_from([&] { 
            start = steady_clock::now();
          }),
          copy_files(scheduler, from, to)
          | then([&](size_t bytesCopied) {
            finish = steady_clock::now();
            printf("copied %ldb\n", bytesCopied);
            fflush(stdout); 
          }))
          | with_query_value(get_scheduler, scheduler));

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
