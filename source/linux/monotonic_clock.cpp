/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unifex/linux/monotonic_clock.hpp>

#include <time.h>

namespace unifex::linuxos {

monotonic_clock::time_point monotonic_clock::now() noexcept {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return time_point::from_seconds_and_nanoseconds(ts.tv_sec, ts.tv_nsec);
}

} // namespace unifex::linuxos
