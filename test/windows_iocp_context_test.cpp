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

#ifdef _WIN32

#include <unifex/win32/low_latency_iocp_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/let_done.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/materialize.hpp>
#include <unifex/span.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/trampoline_scheduler.hpp>
#include <unifex/typed_via.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/finally.hpp>
#include <unifex/on.hpp>
#include <unifex/defer.hpp>
#include <unifex/just_from.hpp>

#include <chrono>
#include <vector>
#include <cstring>
#include <thread>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(low_latency_iocp_context, construct_destruct) {
    unifex::win32::low_latency_iocp_context context{100};
}

TEST(low_latency_iocp_context, run) {
    unifex::win32::low_latency_iocp_context context{100};
    unifex::inplace_stop_source stopSource;

    std::thread stopperThread{[&] {
        std::this_thread::sleep_for(10ms);
        stopSource.request_stop();
    }};

    context.run(stopSource.get_token());

    stopperThread.join();
}

TEST(low_latency_iocp_context, schedule) {
    unifex::win32::low_latency_iocp_context context{100};

    unifex::inplace_stop_source stopSource;

    std::thread ioThread{[&] { context.run(stopSource.get_token()); }};

    auto s = context.get_scheduler();

    unifex::sync_wait(unifex::schedule(s));

    stopSource.request_stop();
    ioThread.join();
}

TEST(low_latency_iocp_context, schedule_multiple) {
    unifex::win32::low_latency_iocp_context context{100};

    unifex::inplace_stop_source stopSource;

    std::thread ioThread{[&] {
        context.run(stopSource.get_token());
    }};

    auto s = context.get_scheduler();

    unifex::sync_wait(unifex::when_all(
        unifex::schedule(s),
        unifex::then(
            unifex::schedule(s),
            [&]() {
                UNIFEX_ASSERT(std::this_thread::get_id() == ioThread.get_id());
            }),
        unifex::schedule(s)));

    stopSource.request_stop();
    ioThread.join();
}

TEST(low_latency_iocp_context, read_write_pipe) {
    unifex::win32::low_latency_iocp_context context{100};

    unifex::inplace_stop_source stopSource;

    std::thread ioThread{[&] {
        context.run(stopSource.get_token());
    }};

    auto s = context.get_scheduler();

    auto [readPipe, writePipe] = unifex::open_pipe(s);

    char readBuffer[10];
    std::memset(readBuffer, 99, sizeof(readBuffer));

    const char writeBuffer[10] = { 0, 1, 2, 3, 5, 7, 11, 13, 17, 19};

    auto results = unifex::sync_wait(
        unifex::when_all(
            unifex::async_read_some(readPipe, unifex::as_writable_bytes(unifex::span{readBuffer})),
            unifex::async_write_some(writePipe, unifex::as_bytes(unifex::span{writeBuffer}))));

    UNIFEX_ASSERT(results.has_value());

    [[maybe_unused]] auto [bytesRead] = std::get<0>(std::get<0>(results.value()));
    [[maybe_unused]] auto [bytesWritten] = std::get<0>(std::get<1>(results.value()));

    UNIFEX_ASSERT(bytesRead == 10);
    UNIFEX_ASSERT(bytesWritten == 10);

    for (int i = 0; i < 10; ++i) {
        UNIFEX_ASSERT(readBuffer[i] == writeBuffer[i]);
    }

    stopSource.request_stop();
    ioThread.join();
}

template<typename Sender>
auto trampoline(Sender&& sender) {
    return unifex::typed_via((Sender&&)sender, unifex::trampoline_scheduler{});
}

template<typename Sender>
auto repeat_n(Sender&& sender, size_t count) {
    return unifex::repeat_effect_until(
            (Sender&&)sender,
            [count]() mutable {
                if (count == 0) return true;
                --count;
                return false;
            });
}

template<typename Sender>
auto discard_value(Sender&& sender) {
    return unifex::then((Sender&&)sender, [](auto&&...) noexcept {});
}

template<typename Sender>
auto measure_time(Sender&& sender, std::string tag = {}) {
    using namespace std::chrono;

    return unifex::let_value_with(
        [] { return steady_clock::now(); },
        [sender=(Sender&&)sender, tag=std::move(tag)](const steady_clock::time_point& startTime) {
            return unifex::finally(
                std::move(sender),
                unifex::just_from([&]() noexcept {
                    auto dur = steady_clock::now() - startTime;
                    auto durUs = duration_cast<microseconds>(dur).count();
                    std::printf("[%s] took %ius\n", tag.c_str(), (int)durUs);
                }));
        });
}


TEST(low_latency_iocp_context, loop_read_write_pipe) {
    unifex::win32::low_latency_iocp_context context{100};

    unifex::inplace_stop_source stopSource;

    std::thread ioThread{[&] {
        context.run(stopSource.get_token());
    }};

    auto s = context.get_scheduler();

    auto [readPipe, writePipe] = unifex::open_pipe(s);

    std::byte readBuffer[10];
    std::byte writeBuffer[100];
    std::memset(writeBuffer, 77, sizeof(writeBuffer));

    // Perform 10k reads of 10 bytes.
    // and      1k writes of 100 bytes
    // and do this asynchronously, interleaving the two loops.
    unifex::sync_wait(
        measure_time(
            unifex::on(
                s,
                unifex::when_all(
                    repeat_n(
                        unifex::defer([&, &readPipe=readPipe] {
                            return discard_value(
                                unifex::async_read_some(readPipe, unifex::span{readBuffer}));
                        }), 10'000),
                    repeat_n(
                        unifex::defer([&, &writePipe=writePipe] {
                            return discard_value(
                                unifex::async_write_some(writePipe, unifex::span{writeBuffer}));
                        }), 1'000))),
            "read + write"));

    stopSource.request_stop();
    ioThread.join();
}

auto write_new_file(
    unifex::win32::low_latency_iocp_context::scheduler s,
    const char* path,
    const std::vector<std::uint8_t>& buffer) {
    using namespace unifex;
    return let_value_with(
        [s, path]() { return open_file_write_only(s, path); },
        [&](win32::low_latency_iocp_context::async_write_only_file& file) {
          return discard_value(when_all(
              async_write_some_at(
                  file, 0 * 6, as_bytes(span{buffer.data() + 0 * 6, 6})),
              async_write_some_at(
                  file, 1 * 6, as_bytes(span{buffer.data() + 1 * 6, 6})),
              async_write_some_at(
                  file, 2 * 6, as_bytes(span{buffer.data() + 2 * 6, 6})),
              async_write_some_at(
                  file, 3 * 6, as_bytes(span{buffer.data() + 3 * 6, 6})),
              async_write_some_at(
                  file, 4 * 6, as_bytes(span{buffer.data() + 4 * 6, 6})),
              async_write_some_at(
                  file, 5 * 6, as_bytes(span{buffer.data() + 5 * 6, 6})),
              async_write_some_at(
                  file, 6 * 6, as_bytes(span{buffer.data() + 6 * 6, 6})),
              async_write_some_at(
                  file, 7 * 6, as_bytes(span{buffer.data() + 7 * 6, 6}))));
        });
}

auto read_ro_file(
    unifex::win32::low_latency_iocp_context::scheduler s,
    const char* path,
    std::vector<std::uint8_t>& buffer) {
    using namespace unifex;
    return let_value_with(
        [s, path]() { return open_file_read_only(s, path); },
        [&buffer](win32::low_latency_iocp_context::async_read_only_file& file) {
          buffer.resize(128);
          return then(
              async_read_some_at(
                  file,
                  0,
                  as_writable_bytes(span{buffer.data(), buffer.size()})),
              [&buffer](std::size_t bytesRead) { buffer.resize(bytesRead); });
        });
}

auto read_rw_file(
    unifex::win32::low_latency_iocp_context::scheduler s,
    const char* path,
    std::vector<std::uint8_t>& buffer) {
    using namespace unifex;
    return let_value_with(
        [s, path]() { return open_file_read_write(s, path); },
        [&buffer](win32::low_latency_iocp_context::async_read_write_file& file) {
          buffer.resize(128);
          return then(
              async_read_some_at(
                  file,
                  0,
                  as_writable_bytes(span{buffer.data(), buffer.size()})),
              [&buffer](std::size_t bytesRead) { buffer.resize(bytesRead); });
        });
}

TEST(low_latency_iocp_context, read_write_file) {
    using namespace unifex;

    win32::low_latency_iocp_context context{100};

    inplace_stop_source stopSource;

    std::thread ioThread{[&]() {
      context.run(stopSource.get_token());
    }};

    auto s = context.get_scheduler();

    const auto data = std::vector<std::uint8_t> {
        '0', '1', '2', '3', '4', '\n',
        '5', '6', '7', '8', '9', '\n',
        'a', 'b', 'c', 'd', 'e', '\n',
        'f', 'g', 'h', 'i', 'j', '\n',
        'k', 'l', 'm', 'n', 'o', '\n',
        'p', 'q', 'r', 's', 't', '\n',
        'u', 'v', 'w', 'x', 'y', '\n',
        'z', '+', '-', '*', '/', '\n'};

    const auto filepath = "low_latency_iocp_context.read_write_file.txt";

    std::vector<std::uint8_t> roFileBuffer;
    std::vector<std::uint8_t> rwFileBuffer;
    sync_wait(sequence(
        write_new_file(s, filepath, data),
        just_from([]() { std::this_thread::sleep_for(1s); }),
        when_all(
            read_ro_file(s, filepath, roFileBuffer),
            read_rw_file(s, filepath, rwFileBuffer))));

    stopSource.request_stop();
    ioThread.join();

    EXPECT_EQ(data, roFileBuffer);
    EXPECT_EQ(data, rwFileBuffer);
}

#endif // _WIN32
