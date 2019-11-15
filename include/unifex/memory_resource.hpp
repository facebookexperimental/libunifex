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

#ifndef UNIFEX_NO_MEMORY_RESOURCE
#if __has_include(<memory_resource>)
# define UNIFEX_NO_MEMORY_RESOURCE 0
# include <memory_resource>
namespace unifex {
    namespace pmr = std::pmr;
}
#elif __has_include(<experimental/memory_resource>)
# define UNIFEX_NO_MEMORY_RESOURCE 0
# include <experimental/memory_resource>
namespace unifex {
    namespace pmr = std::experimental::pmr;
}
#else
# define UNIFEX_NO_MEMORY_RESOURCE 1
#endif
#endif
