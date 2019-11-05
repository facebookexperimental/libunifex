/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unifex/linux/safe_file_descriptor.hpp>

#include <cassert>

#include <unistd.h>

namespace unifex::linux {

void safe_file_descriptor::close() noexcept {
  assert(valid());
  [[maybe_unused]] int result = ::close(std::exchange(fd_, -1));
  assert(result == 0);
}

} // namespace unifex::linux
