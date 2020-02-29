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

#if __cplusplus >= 201703L
# ifdef __has_include
#  if __has_include(<version>)
#   include <version>
#   if __cpp_lib_filesystem >= 201703L && __has_include(<filesystem>)
#    include <filesystem>
#    define UNIFEX_HAVE_FILESYSTEM 1
namespace unifex
{
    namespace filesystem
    {
        using std::filesystem::path;
    }
}
#   endif
#  endif
#  ifndef UNIFEX_HAVE_FILESYSTEM
#   if __has_include(<experimental/filesystem>)
#    include <experimental/filesystem>
#    define UNIFEX_HAVE_FILESYSTEM 1
namespace unifex
{
    namespace filesystem
    {
        using std::experimental::filesystem::path;
    }
}
#   endif
#  endif
# endif
#endif
#ifndef UNIFEX_HAVE_FILESYSTEM
# define UNIFEX_HAVE_FILESYSTEM 0
#endif
