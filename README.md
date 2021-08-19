# Overview

The 'libunifex' project is a prototype implementation of the C++ sender/receiver
async programming model that is currently being considered for standardisation.

This project contains implementations of the following:
* Schedulers
* Timers
* Asynchronous I/O (Linux w/ io_uring)
* Algorithms that encapsulate certain concurrency patterns
* Async streams
* Cancellation
* Coroutine integration

# Status

This project is still evolving and should be considered experimental in nature. No guarantee is made for API or ABI stability.

**Build status**
- on Github Actions: [![GitHub Actions Status](https://github.com/facebookexperimental/libunifex/workflows/libunifex%20CI/badge.svg?branch=master)](https://github.com/facebookexperimental/libunifex/actions)

# Documentation

* [Overview](doc/overview.md)
* [Concepts](doc/concepts.md)
* [API Reference](doc/api_reference.md)
* Topics
  * [Cancellation](doc/cancellation.md)
  * [Debugging](doc/debugging.md)
  * [Customisation Points](doc/customisation_points.md)

# Requirements

A recent compiler that supports C++17 or later. Libunifex is known to work
with the following compilers:

* GCC, 9.x and later
* Clang, 10.x and later
* MSVC 2019.6 and later

This library also supports C++20 coroutines. You will need to compile with
coroutine support enabled if you want to use the coroutine integrations.
This generally means adding `-std=c++2a` or `-fcoroutines-ts` on Clang (see "Configuring" below).

## Linux

The io_uring support on Linux requires a recent kernel version
(5.6 or later).

See http://git.kernel.dk/cgit/linux-block/log/?h=for-5.5/io_uring

The io_uring support depends on liburing: https://github.com/axboe/liburing/

## Windows

`windows_thread_pool` executor requires Windows Vista or later.

# Building

This project can be built using CMake.

The examples below assume using the [Ninja](https://ninja-build.org/) build system.
You can use other build systems supported by CMake.

## Configuring

First generate the build files under the `./build` subdirectory.

From the libunifex project root:

```sh
cmake -G Ninja -H. -Bbuild \
      -DCMAKE_CXX_COMPILER:PATH=/path/to/compiler
```

By default, this builds libunifex in C++17 without coroutines. If you want
to turn on coroutines with clang, add:

```sh
      -DCMAKE_CXX_FLAGS:STRING=-fcoroutines-ts
```

To use libc++ with clang, which has coroutine support, you should also add:

```sh
      -DCMAKE_CXX_FLAGS:STRING=-stdlib=libc++ \
      -DCMAKE_EXE_LINKER_FLAGS:STRING="-L/path/to/libc++/lib"
```

If you want to build libunifex as C++20, add:

```sh
      -DCMAKE_CXX_STANDARD:STRING=20
```

## Building Library + Running Tests

To build the library and tests.

From the `./build` subdirectory run:
```sh
ninja
```

Once the tests have been built you can run them.

From the `./build` subdirectory run:
```sh
ninja test
```

# License

This project is made available under the Apache License, version 2.0, with LLVM Exceptions.

See [LICENSE.txt](LICENSE.txt) for details.

See also:
* Terms of Use - https://opensource.facebook.com/legal/terms
* Privacy Policy - https://opensource.facebook.com/legal/privacy

# References

C++ standardisation papers:
* [P2300r1](https://wg21.link/P2300r1) "`std::execution`"
* ...
