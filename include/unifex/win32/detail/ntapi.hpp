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
#pragma once

#include <cstdint>

// Mirror definition of NTAPI without depending on windows headers.
#if (_MSC_VER >= 800) || defined(_STDCALL_SUPPORTED)
#  define UNIFEX_NTAPI __stdcall
#else
#  define _cdecl
#  define __cdecl
#  define UNIFEX_NTAPI
#endif

#ifdef _MSC_VER
#  include <sal.h>
#else
#  define _In_
#  define _In_opt_
#  define _Out_
#  define _Out_writes_to(A, B)
#endif

namespace unifex::win32::ntapi
{
  using HANDLE = void*;
  using PHANDLE = HANDLE*;
  using LONG = long;
  using NTSTATUS = LONG;
  using ULONG_PTR = std::uintptr_t;
  using LONG_PTR = std::intptr_t;
  using PVOID = void*;
  using USHORT = unsigned short;
  using LONG = long;
  using LONGLONG = long long;
  using ULONG = unsigned long;
  using PULONG = ULONG*;
  using DWORD = unsigned long;
  using WCHAR = wchar_t;
  using PWSTR = WCHAR*;
  using BYTE = unsigned char;
  using BOOLEAN = BYTE;

  union LARGE_INTEGER {
    struct {
      DWORD LowPart;
      LONG HighPart;
    } u;
    LONGLONG QuadPart;
  };
  using PLARGE_INTEGER = LARGE_INTEGER*;

  struct UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
  };
  using PUNICODE_STRING = UNICODE_STRING*;

  struct IO_STATUS_BLOCK {
    // Corresponds to OVERLAPPED::Internal
    union {
      NTSTATUS Status;
      void* Pointer;
    };

    // Corresponds to OVERLAPPED::InternalHigh
    ULONG_PTR Information;
  };
  using PIO_STATUS_BLOCK = IO_STATUS_BLOCK*;

  using ACCESS_MASK = DWORD;
  using PACCESS_MASK = ACCESS_MASK*;

  struct OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
  };
  using POBJECT_ATTRIBUTES = OBJECT_ATTRIBUTES*;

  struct FILE_COMPLETION_INFORMATION {
    HANDLE Port;
    PVOID Key;
  };
  using PFILE_COMPLETION_INFORMATION = FILE_COMPLETION_INFORMATION*;

  struct FILE_IO_COMPLETION_INFORMATION {
    PVOID KeyContext;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock;
  };
  using PFILE_IO_COMPLETION_INFORMATION = FILE_IO_COMPLETION_INFORMATION*;

  using PIO_APC_ROUTINE = void(UNIFEX_NTAPI*)(
      _In_ PVOID ApcContext,
      _In_ PIO_STATUS_BLOCK IoStatusBlock,
      _In_ ULONG Reserved);

  // From
  // http://msdn.microsoft.com/en-us/library/windows/hardware/ff566424(v=vs.85).aspx
  using NtCreateFile_t = NTSTATUS(UNIFEX_NTAPI*)(
      _Out_ PHANDLE FileHandle,
      _In_ ACCESS_MASK DesiredAccess,
      _In_ POBJECT_ATTRIBUTES ObjectAttributes,
      _Out_ PIO_STATUS_BLOCK IoStatusBlock,
      _In_opt_ PLARGE_INTEGER AllocationSize,
      _In_ ULONG FileAttributes,
      _In_ ULONG ShareAccess,
      _In_ ULONG CreateDisposition,
      _In_ ULONG CreateOptions,
      _In_opt_ PVOID EaBuffer,
      _In_ ULONG EaLength);
  extern NtCreateFile_t NtCreateFile;

  using NtCancelIoFileEx_t = NTSTATUS(UNIFEX_NTAPI*)(
      _In_ HANDLE hFile,
      _Out_ PIO_STATUS_BLOCK iosb,
      _Out_ PIO_STATUS_BLOCK io_status);
  extern NtCancelIoFileEx_t NtCancelIoFileEx;

  using NtReadFile_t = NTSTATUS(UNIFEX_NTAPI*)(
      _In_ HANDLE FileHandle,
      _In_opt_ HANDLE Event,
      _In_opt_ PIO_APC_ROUTINE ApcRoutine,
      _In_opt_ PVOID ApcContext,
      _Out_ PIO_STATUS_BLOCK IoStatusBlock,
      _Out_ PVOID Buffer,
      _In_ ULONG Length,
      _In_opt_ PLARGE_INTEGER ByteOffset,
      _In_opt_ PULONG Key);
  extern NtReadFile_t NtReadFile;

  using NtWriteFile_t = NTSTATUS(UNIFEX_NTAPI*)(
      _In_ HANDLE FileHandle,
      _In_opt_ HANDLE Event,
      _In_opt_ PIO_APC_ROUTINE ApcRoutine,
      _In_opt_ PVOID ApcContext,
      _Out_ PIO_STATUS_BLOCK IoStatusBlock,
      _In_ PVOID Buffer,
      _In_ ULONG Length,
      _In_opt_ PLARGE_INTEGER ByteOffset,
      _In_opt_ PULONG Key);
  extern NtWriteFile_t NtWriteFile;

  using NtSetIoCompletion_t = NTSTATUS(UNIFEX_NTAPI*)(
      _In_ HANDLE IoCompletionHandle,
      _In_ ULONG KeyContext,
      _In_ PVOID ApcContext,
      _In_ NTSTATUS IoStatus,
      _In_ ULONG IoStatusInformation);
  extern NtSetIoCompletion_t NtSetIoCompletion;

  using NtRemoveIoCompletion_t = NTSTATUS(UNIFEX_NTAPI*)(
      _In_ HANDLE IoCompletionHandle,
      _Out_ PVOID* CompletionKey,
      _Out_ PVOID* ApcContext,
      _Out_ PIO_STATUS_BLOCK IoStatusBlock,
      _In_opt_ PLARGE_INTEGER Timeout);
  extern NtRemoveIoCompletion_t NtRemoveIoCompletion;

  using NtRemoveIoCompletionEx_t = NTSTATUS(UNIFEX_NTAPI*)(
      _In_ HANDLE IoCompletionHandle,
      _Out_writes_to_(Count, *NumEntriesRemoved)
          PFILE_IO_COMPLETION_INFORMATION IoCompletionInformation,
      _In_ ULONG Count,
      _Out_ PULONG NumEntriesRemoved,
      _In_opt_ PLARGE_INTEGER Timeout,
      _In_ BOOLEAN Alertable);
  extern NtRemoveIoCompletionEx_t NtRemoveIoCompletionEx;

  using RtlNtStatusToDosError_t = ULONG(UNIFEX_NTAPI*)(_In_ NTSTATUS Status);
  extern RtlNtStatusToDosError_t RtlNtStatusToDosError;

  void ensure_initialised() noexcept;

  constexpr bool ntstatus_success(NTSTATUS status) { return status >= 0; }

}  // namespace unifex::win32::ntapi

#undef UNIFEX_NTAPI
