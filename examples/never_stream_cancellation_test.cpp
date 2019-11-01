#include <unifex/for_each.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/never.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/scope_guard.hpp>

#include <chrono>
#include <cstdio>
#include <optional>
#include <thread>

using namespace std::literals::chrono_literals;
using namespace unifex;

int main() {
  inplace_stop_source stopSource;

  std::thread t{[&] {
    std::this_thread::sleep_for(100ms);

    std::printf("requesting stop\n");
    std::fflush(stdout);

    stopSource.request_stop();

    std::printf("request_stop() returned\n");
    std::fflush(stdout);
  }};
  scope_guard joinThread = [&]() noexcept { t.join(); };

  std::optional<unit> result = sync_wait(
      cpo::for_each(
          never_stream{},
          [](auto) {
            std::printf("got value");
            std::fflush(stdout);
          }),
      stopSource.get_token());

  std::printf("completed with %s\n", result ? "unit" : "nullopt");

  return 0;
}
