#include <unifex/scheduler_concepts.hpp>
#include <unifex/inline_scheduler.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/via.hpp>

#include <cstdio>

using namespace unifex;

int main() {
  inline_scheduler scheduler;
  sync_wait_r<void>(
      via(cpo::schedule(scheduler), transform(cpo::schedule(scheduler), []() {
            std::printf("Hello from inline_scheduler");
          })));
  return 0;
}
