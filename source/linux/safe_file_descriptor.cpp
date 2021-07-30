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
#include <unifex/linux/safe_file_descriptor.hpp>


#include <unistd.h>

namespace unifex::linuxos {

void safe_file_descriptor::close() noexcept {
  UNIFEX_ASSERT(valid());
  [[maybe_unused]] int result = ::close(std::exchange(fd_, -1));
  UNIFEX_ASSERT(result == 0);
}

} // namespace unifex::linuxos
