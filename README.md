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

This project is still evolving and should be considered experimental in nature.

**Build status**
- on Travis-CI: [![Travis Build Status](https://travis-ci.com/facebookexperimental/libunifex.svg?branch=master)](https://travis-ci.com/facebookexperimental/libunifex)

# Documentation

* [Overview](doc/overview.md)
* [Concepts](doc/concepts.md)
* [API Reference](doc/api_reference.md)
* Topics
  * [Cancellation](doc/cancellation.md)
  * [Debugging](doc/debugging.md)
  * [Customisation Points](doc/customisation_points.md)

# Requirements

A recent compiler that supports C++17 or later.

This library also supports C++20 coroutines. You will need to compile with
coroutine support enabled if you want to use the coroutine integrations.
This generally means adding `-std=c++2a` or `-fcoroutines-ts` on Clang.

## Linux

The io_uring support on Linux requires a bleeding edge kernel version
that incorporates patches from recent io_uring development.

See http://git.kernel.dk/cgit/linux-block/log/?h=for-5.5/io_uring

The io_uring support depends on liburing: https://github.com/axboe/liburing/

# Building

This project can be built using CMake.

The examples below assume using the [Ninja](https://ninja-build.org/) build system.
You can use other build systems supported by CMake.

## Configuring to build with Clang

First generate the build files under the `./build` subdirectory.

From the libunifex project root:
```sh
cmake -G Ninja -H. -Bbuild \
      -DCMAKE_CXX_COMPILER=/path/to/clang++ \
      -DCMAKE_CXX_FLAGS="-std=c++2a" \
      -DCMAKE_EXE_LINKER_FLAGS="-L/path/to/libc++/lib"
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

This project is made available under the Apache License, version 2.0.

See [LICENSE.txt](license.txt) for details.

# References

C++ standardisation papers:
* [P0443R11](https://wg21.link) "A Unified Executors Proposal for C++"
* ...
