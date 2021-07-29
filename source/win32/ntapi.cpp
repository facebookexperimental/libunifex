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
#include <unifex/win32/detail/ntapi.hpp>

#include <exception>
#include <type_traits>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace unifex::win32::ntapi
{
  NtCreateFile_t NtCreateFile = nullptr;
  NtCancelIoFileEx_t NtCancelIoFileEx = nullptr;
  NtReadFile_t NtReadFile = nullptr;
  NtWriteFile_t NtWriteFile = nullptr;
  NtSetIoCompletion_t NtSetIoCompletion = nullptr;
  NtRemoveIoCompletion_t NtRemoveIoCompletion = nullptr;
  NtRemoveIoCompletionEx_t NtRemoveIoCompletionEx = nullptr;
  RtlNtStatusToDosError_t RtlNtStatusToDosError = nullptr;

  static void do_initialisation() noexcept {
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == NULL) {
      std::terminate();
    }

    auto init = [&](auto& fnPtr, const char* name) noexcept {
      FARPROC p = ::GetProcAddress(ntdll, name);
      if (p == NULL) {
        std::terminate();
      }
      fnPtr = reinterpret_cast<std::remove_reference_t<decltype(fnPtr)>>(p);
    };

    init(NtCreateFile, "NtCreateFile");
    init(NtCancelIoFileEx, "NtCancelIoFileEx");
    init(NtReadFile, "NtReadFile");
    init(NtWriteFile, "NtWriteFile");
    init(NtSetIoCompletion, "NtSetIoCompletion");
    init(NtRemoveIoCompletion, "NtRemoveIoCompletion");
    init(NtRemoveIoCompletionEx, "NtRemoveIoCompletionEx");
    init(RtlNtStatusToDosError, "RtlNtStatusToDosError");
  }

  void ensure_initialised() noexcept {
    static struct initialiser {
      initialiser() noexcept { do_initialisation(); }
    } dummy;
  }
}  // namespace unifex::win32::ntapi
