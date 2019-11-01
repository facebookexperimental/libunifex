#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/transform.hpp>
#include <unifex/typed_via.hpp>
#include <unifex/when_all.hpp>

#include <chrono>
#include <iostream>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

int main() {
  timed_single_thread_context context;
  auto scheduler = context.get_scheduler();

  auto start = steady_clock::now();

  sync_wait(transform(
      when_all(
          transform(
              cpo::schedule_after(scheduler, 100ms),
              [=]() {
                auto time = steady_clock::now() - start;
                auto timeMs = duration_cast<milliseconds>(time).count();
                std::cout << "part1 finished - [" << timeMs << "]\n";
                return time;
              }),
          transform(
              cpo::schedule_after(scheduler, 200ms),
              [=]() {
                auto time = steady_clock::now() - start;
                auto timeMs = duration_cast<milliseconds>(time).count();
                std::cout << "part2 finished - [" << timeMs << "]\n";
                return time;
              })),
      [](auto&& a, auto&& b) {
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
