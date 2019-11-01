#include <unifex/delay.hpp>
#include <unifex/for_each.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/single.hpp>
#include <unifex/stop_immediately.hpp>
#include <unifex/take_until.hpp>
#include <unifex/thread_unsafe_event_loop.hpp>
#include <unifex/typed_via_stream.hpp>

#include <chrono>
#include <cstdio>
#include <optional>

using namespace unifex;
using namespace std::chrono;

int main() {
  thread_unsafe_event_loop eventLoop;

  std::printf("starting\n");

  auto start = steady_clock::now();

  [[maybe_unused]] std::optional<unit> result =
      eventLoop.sync_wait(cpo::for_each(
          take_until(
              stop_immediately<int>(typed_via_stream(
                  delay(eventLoop.get_scheduler(), 50ms),
                  range_stream{0, 100})),
              single(eventLoop.get_scheduler().schedule_after(500ms))),
          [start](int value) {
            auto ms = duration_cast<milliseconds>(steady_clock::now() - start);
            std::printf("[%i ms] %i\n", (int)ms.count(), value);
          }));

  return 0;
}
