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

#include <unifex/win32/windows_thread_pool.hpp>

#include <unifex/exception.hpp>

#include <windows.h>

namespace unifex::win32 {

windows_thread_pool::windows_thread_pool() noexcept
: threadPool_(nullptr)
{}

windows_thread_pool::windows_thread_pool(std::uint32_t minThreadCount, std::uint32_t maxThreadCount)
: threadPool_(::CreateThreadpool(nullptr)) {
    if (threadPool_ == nullptr) {
        DWORD errorCode = ::GetLastError();
        throw_(std::system_error{static_cast<int>(errorCode), std::system_category(), "CreateThreadPool()"});
    }

    ::SetThreadpoolThreadMaximum(threadPool_, maxThreadCount);
    if (!::SetThreadpoolThreadMinimum(threadPool_, minThreadCount)) {
        DWORD errorCode = ::GetLastError();
        ::CloseThreadpool(threadPool_);
        throw_(std::system_error{static_cast<int>(errorCode), std::system_category(), "SetThreadpoolThreadMinimum()"});
    }
}

windows_thread_pool::~windows_thread_pool() {
    if (threadPool_ != nullptr){
        ::CloseThreadpool(threadPool_);
    }
}

windows_thread_pool::schedule_op_base::~schedule_op_base() {
    ::CloseThreadpoolWork(work_);
    ::DestroyThreadpoolEnvironment(&environ_);
}

void windows_thread_pool::schedule_op_base::start() & noexcept {
    ::SubmitThreadpoolWork(work_);
}

windows_thread_pool::schedule_op_base::schedule_op_base(windows_thread_pool& pool, PTP_WORK_CALLBACK workCallback)
{
    ::InitializeThreadpoolEnvironment(&environ_);
    ::SetThreadpoolCallbackPool(&environ_, pool.threadPool_);
    work_ = ::CreateThreadpoolWork(workCallback, this, &environ_);
    if (work_ == nullptr) {
        // TODO: Should we just cache the error and deliver via set_error(receiver_, std::error_code{})
        // upon start()?
        DWORD errorCode = ::GetLastError();
        ::DestroyThreadpoolEnvironment(&environ_);
        throw_(std::system_error{static_cast<int>(errorCode), std::system_category(), "CreateThreadpoolWork()"});
    }
}

} // namespace unifex::win32
