
#include <chrono>
#include <iostream>

#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#if !UNIFEX_NO_COROUTINES
#  include <unifex/task.hpp>
#endif  // UNIFEX_NO_COROUTINES
#include <unifex/timed_single_thread_context.hpp>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace std;

timed_single_thread_context timer;
auto delay(milliseconds ms) {
  return schedule_after(timer.get_scheduler(), ms);
}

#if !UNIFEX_NO_COROUTINES
unifex::task<void> asyncMain() {
  co_await delay(1000ms);
}
#endif  // UNIFEX_NO_COROUTINES

int main() {
  auto start_time = steady_clock::now();
#if !UNIFEX_NO_COROUTINES
  sync_wait(asyncMain());
#endif  // UNIFEX_NO_COROUTINES
  cout << "Total time is: "
       << duration_cast<std::chrono::milliseconds>(
              steady_clock::now() - start_time)
              .count()
       << "ms\n";
  return 0;
}
