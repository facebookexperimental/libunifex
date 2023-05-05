
#include <chrono>
#include <iostream>

#include <unifex/on.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace std;

auto delay(milliseconds ms) {
  return schedule_after(current_scheduler, ms);
}

timed_single_thread_context timer;
void sleep() {
  on(timer.get_scheduler(), delay(1000ms)) | sync_wait();
}

int main() {
  auto start_time = steady_clock::now();
  sleep();
  cout << "Total time is: "
       << duration_cast<std::chrono::milliseconds>(
              steady_clock::now() - start_time)
              .count()
       << "ms\n";
  return 0;
}
