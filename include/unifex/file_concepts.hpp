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

#include <unifex/tag_invoke.hpp>

#include <unifex/io_concepts.hpp>

#include <unifex/filesystem.hpp>

namespace unifex {
namespace _filesystem {
inline constexpr struct open_file_read_only_cpo {
  template <typename Executor>
  auto operator()(Executor&& executor, const filesystem::path& path) const
      noexcept(is_nothrow_tag_invocable_v<
               open_file_read_only_cpo,
               Executor,
               const filesystem::path&>)
          -> tag_invoke_result_t<
              open_file_read_only_cpo,
              Executor,
              const filesystem::path&> {
    return unifex::tag_invoke(*this, (Executor &&) executor, path);
  }
} open_file_read_only{};

inline constexpr struct open_file_write_only_cpo {
  template <typename Executor>
  auto operator()(Executor&& executor, const filesystem::path& path) const
      noexcept(is_nothrow_tag_invocable_v<
               open_file_write_only_cpo,
               Executor,
               const filesystem::path&>)
          -> tag_invoke_result_t<
              open_file_write_only_cpo,
              Executor,
              const filesystem::path&> {
    return unifex::tag_invoke(*this, (Executor &&) executor, path);
  }
} open_file_write_only{};

inline constexpr struct open_file_read_write_cpo {
  template <typename Executor>
  auto operator()(Executor&& executor, const filesystem::path& path) const
      noexcept(is_nothrow_tag_invocable_v<
               open_file_read_write_cpo,
               Executor,
               const filesystem::path&>)
          -> tag_invoke_result_t<
              open_file_read_write_cpo,
              Executor,
              const filesystem::path&> {
    return unifex::tag_invoke(*this, (Executor &&) executor, path);
  }
} open_file_read_write{};
} // namespace _filesystem

using _filesystem::open_file_read_only;
using _filesystem::open_file_write_only;
using _filesystem::open_file_read_write;
} // namespace unifex
