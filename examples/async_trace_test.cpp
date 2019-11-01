#include <unifex/async_trace.hpp>
#include <unifex/awaitable_sender.hpp>
#include <unifex/on.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_awaitable.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/transform.hpp>
#include <unifex/typed_via.hpp>
#include <unifex/when_all.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

auto dump_async_trace(std::string tag = {}) {
  return transform(
      async_trace_sender{},
      [tag = std::move(tag)](const std::vector<async_trace_entry>& entries) {
        std::cout << "Async Trace (" << tag << "):\n";
        for (auto& entry : entries) {
          std::cout << " " << entry.depth << " [-> " << entry.parentIndex
                    << "]: " << entry.continuation.type().name() << " @ 0x";
          std::cout.setf(std::ios::hex, std::ios::basefield);
          std::cout << entry.continuation.address();
          std::cout.unsetf(std::ios::hex);
          std::cout << "\n";
        }
      });
}

template <typename Sender>
auto dump_async_trace_on_start(Sender&& sender, std::string tag = {}) {
  return unifex::on(dump_async_trace(std::move(tag)), (Sender &&) sender);
}

template <typename Sender>
auto dump_async_trace_on_completion(Sender&& sender, std::string tag = {}) {
  return unifex::typed_via(
      (Sender &&) sender, dump_async_trace(std::move(tag)));
}

int main() {
  timed_single_thread_context context;

  auto start = steady_clock::now();

  sync_wait(transform(
      when_all(
          transform(
              dump_async_trace_on_start(
                  cpo::schedule_after(context.get_scheduler(), 100ms), "part1"),
              [=]() {
                auto time = steady_clock::now() - start;
                auto timeMs = duration_cast<milliseconds>(time).count();
                std::cout << "part1 finished - [" << timeMs << "]\n";
                return time;
              }),
          transform(
              dump_async_trace_on_completion(
                  cpo::schedule_after(context.get_scheduler(), 200ms), "part2"),
              [=]() {
                auto time = steady_clock::now() - start;
                auto timeMs = duration_cast<milliseconds>(time).count();
                std::cout << "part2 finished - [" << timeMs << "]\n";
                return time;
              }),
          awaitable_sender{[]() -> task<int> {
            co_await dump_async_trace("coroutine");
            co_return 42;
          }()}),
      [](auto&& a, auto&& b, auto&& c) {
        std::cout
            << "when_all finished - ["
            << duration_cast<milliseconds>(std::get<0>(std::get<0>(a))).count()
            << ", "
            << duration_cast<milliseconds>(std::get<0>(std::get<0>(b))).count()
            << "]\n";
      }));

  std::cout << "all done\n";

  return 0;
}
