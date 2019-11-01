#pragma once

#include <unifex/config.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <type_traits>

namespace unifex {

template <typename Scheduler, typename Duration>
struct delayed_scheduler {
  Scheduler scheduler_;
  Duration duration_;

  auto schedule() {
    return cpo::schedule_after(scheduler_, duration_);
  }
};

template <typename Scheduler, typename Duration>
auto delay(Scheduler&& scheduler, Duration&& duration) {
  return delayed_scheduler<
      std::remove_cvref_t<Scheduler>,
      std::remove_cvref_t<Duration>>{(Scheduler &&) scheduler,
                                     (Duration &&) duration};
}

} // namespace unifex
