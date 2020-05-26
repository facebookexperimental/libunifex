/*
 * Copyright 2020-present Facebook, Inc.
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

#include <unifex/win32/filetime_clock.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace unifex::win32 {

filetime_clock::time_point filetime_clock::now() noexcept {
    FILETIME filetime;
    ::GetSystemTimeAsFileTime(&filetime);

    ULARGE_INTEGER ticks;
    ticks.HighPart = filetime.dwHighDateTime;
    ticks.LowPart = filetime.dwLowDateTime;

    return time_point::from_ticks(ticks.QuadPart);
}

} // namespace unifex::win32
