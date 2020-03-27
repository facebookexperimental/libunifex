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

A recent compiler that supports C++20 or later.

This library supports C++20 implementations without coroutines. Working
support is automatically detected and enabled if available.

## Linux

The io_uring support on Linux requires a bleeding edge kernel version
that incorporates patches from recent io_uring development.

See http://git.kernel.dk/cgit/linux-block/log/?h=for-5.5/io_uring

# Building

This project can be built using CMake.

The examples below assume using the [Ninja](https://ninja-build.org/) build system.
You can use other build systems supported by CMake.

At the time of writing, libstdc++ does not implement sufficient support
for C++ 20 coroutines to be usable. One way of using C++ 20 coroutines
today is to use libc++ instead of libstdc++.

## Configuring to build with Clang

First generate the build files under the `./build` subdirectory.

From the libunifex project root:
```sh
export CC=gcc-9
export CXX=g++-9
cmake -G Ninja -H. -Bbuild \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

If you wish to use libc++ on Linux for the coroutines implementation,
you will need to install a very recent libc++, libc++abi and clang. You
must then create a cmake toolchain file to tell cmake to use an
alternative STL implementation:

```cmake
set(CMAKE_C_COMPILER clang-10)
set(CMAKE_CXX_COMPILER clang++-10)
set(CMAKE_CXX_FLAGS -stdlib=libc++)
set(CMAKE_EXE_LINKER_FLAGS -stdlib=libc++)
```

Then configure as follows:

```sh
cmake -G Ninja -H. -Bbuild \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_TOOLCHAIN_FILE=linux-libc++.toolchain.file
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
