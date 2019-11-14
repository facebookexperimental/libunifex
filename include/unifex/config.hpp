/*
 * Copyright 2019-present Facebook, Inc.
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
#pragma once

#if __GNUG__ && !__clang__
#define UNIFEX_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define UNIFEX_NO_UNIQUE_ADDRESS
#endif

// UNIFEX_NO_COROUTINES is defined to 1 if compiling without coroutine support
// enabled and is defined to 0 if coroutine support is enabled.
#ifndef UNIFEX_NO_COROUTINES
#if __cpp_coroutines >= 201703L && __has_include(<experimental/coroutine>)
# define UNIFEX_NO_COROUTINES 0
#else
# define UNIFEX_NO_COROUTINES 1
#endif
#endif
