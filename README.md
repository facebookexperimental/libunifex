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

# Documentation

* [Overview](doc/overview.md)
* [Concepts](doc/concepts.md)
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

TODO

# License

This project is made available under the Apache License, version 2.0.

See [LICENSE.txt](license.txt) for details.

# References

C++ standardisation papers:
* [P0443R11](https://wg21.link) "A Unified Executors Proposal for C++"
* ...
