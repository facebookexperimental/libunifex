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

#include <unifex/config.hpp>

#if !UNIFEX_NO_LIBURING

#include "io_uring_syscall.hpp"

// Based on liburing's syscall.c by Jens Axboe

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#ifdef __alpha__
/*
 * alpha is the only exception, all other architectures
 * have common numbers for new system calls.
 */
# ifndef __NR_io_uring_setup
#  define __NR_io_uring_setup		535
# endif
# ifndef __NR_io_uring_enter
#  define __NR_io_uring_enter		536
# endif
# ifndef __NR_io_uring_register
#  define __NR_io_uring_register	537
# endif
#else /* !__alpha__ */
# ifndef __NR_io_uring_setup
#  define __NR_io_uring_setup		425
# endif
# ifndef __NR_io_uring_enter
#  define __NR_io_uring_enter		426
# endif
# ifndef __NR_io_uring_register
#  define __NR_io_uring_register	427
# endif
#endif

namespace unifex::linuxos
{
    int io_uring_register(int fd, unsigned opcode, const void *arg,
			    unsigned nr_args)
    {
        return syscall(__NR_io_uring_register, fd, opcode, arg, nr_args);
    }

    int io_uring_setup(unsigned entries, struct io_uring_params *p)
    {
        return syscall(__NR_io_uring_setup, entries, p);
    }

    int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                       unsigned flags, sigset_t *sig)
    {
        return syscall(__NR_io_uring_enter, fd, to_submit, min_complete,
                       flags, sig, _NSIG / 8);
    }
}

#endif // UNIFEX_NO_LIBURING
