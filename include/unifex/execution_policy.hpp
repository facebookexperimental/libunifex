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

#include <unifex/detail/prologue.hpp>

namespace unifex
{
    // Execution policies are used to describe constraints on the safe execution
    // of bulk operations with respect to each other.
    //
    // sequenced - Operations must be sequenced with respect to each other.
    //             They are not safe to executed concurrently on differen threads or to
    //             interleaved with each other on the same thread.
    //
    // unsequenced - Operations are safe to be interleaved with each other, e.g. using vectorised
    //               SIMD instructions, but may not be executed concurrently on different
    //               threads. This generally implies that forward progress of one operation
    //               is not dependent on forward progress of other operations.   
    //
    // parallel - Operations are safe to be executed concurrently with each other on different
    //            threads but operations on each thread must not be interleaved.
    //
    // parallel_unsequenced - Operations are safe to be executed concurrently on different
    //            threads and may be interleaved with each other on the same thread.

    inline constexpr struct sequenced_policy {} seq;
    inline constexpr struct unsequenced_policy {} unseq;
    inline constexpr struct parallel_policy {} par;
    inline constexpr struct parallel_unsequenced_policy {} par_unseq;
}

#include <unifex/detail/epilogue.hpp>
