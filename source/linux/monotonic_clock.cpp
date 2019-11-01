#include <unifex/linux/monotonic_clock.hpp>

#include <time.h>

namespace unifex::linux {

monotonic_clock::time_point monotonic_clock::now() noexcept {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return time_point::from_seconds_and_nanoseconds(ts.tv_sec, ts.tv_nsec);
}

} // namespace unifex::linux
