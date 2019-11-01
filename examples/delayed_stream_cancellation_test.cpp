#include <unifex/delay.hpp>
#include <unifex/for_each.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/typed_via_stream.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace unifex;

int main() {
  using namespace std::chrono;

  timed_single_thread_context context;

  inplace_stop_source stopSource;
  std::thread t{[&] {
    std::this_thread::sleep_for(500ms);
    std::printf("cancelling\n");
    stopSource.request_stop();
  }};
  scope_guard joinThread = [&]() noexcept {
    t.join();
  };

  auto start = steady_clock::now();

  sync_wait(
      cpo::for_each(
          typed_via_stream(
              delay(context.get_scheduler(), 100ms), range_stream{0, 100}),
          [start](int value) {
            auto ms = duration_cast<milliseconds>(steady_clock::now() - start);
            std::printf("[%i ms] %i\n", (int)ms.count(), value);
          }),
      stopSource.get_token());

  return 0;
}
