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

#include <unifex/win32/detail/safe_handle.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace unifex::win32 {

void safe_handle::reset() noexcept {
    handle_t h = std::exchange(handle_, nullptr);
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
        ::CloseHandle(h);
    }
}

} // namespace unifex::win32
