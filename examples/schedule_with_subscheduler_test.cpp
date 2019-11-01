#include <unifex/schedule_with_subscheduler.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/transform.hpp>

using namespace unifex;

int main() {
  timed_single_thread_context context;
  auto scheduler = context.get_scheduler();

  std::optional<bool> result = sync_wait(transform(
      cpo::schedule_with_subscheduler(scheduler),
      [&](auto subScheduler) noexcept { return subScheduler == scheduler; }));

  if (result.has_value() && result.value()) {
    // Success
    return 0;
  }

  return 1;
}
