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

#include <unifex/config.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/filesystem.hpp>

namespace unifex {
namespace _filesystem {
UNIFEX_INLINE_VAR constexpr struct async_read_some_at_cpo {
  template <typename AsyncFile, typename BufferSequence>
  auto operator()(
      AsyncFile& file,
      typename AsyncFile::offset_t offset,
      BufferSequence&& bufferSequence) const
      noexcept(is_nothrow_tag_invocable_v<
               async_read_some_at_cpo,
               AsyncFile&,
               typename AsyncFile::offset_t,
               BufferSequence>)
          -> tag_invoke_result_t<
              async_read_some_at_cpo,
              AsyncFile&,
              typename AsyncFile::offset_t,
              BufferSequence> {
    return unifex::tag_invoke(
        *this, file, offset, (BufferSequence &&) bufferSequence);
  }
} async_read_some_at{};

UNIFEX_INLINE_VAR constexpr struct async_write_some_at_cpo {
  template <typename AsyncFile, typename BufferSequence>
  auto operator()(
      AsyncFile& file,
      typename AsyncFile::offset_t offset,
      BufferSequence&& bufferSequence) const
      noexcept(is_nothrow_tag_invocable_v<
               async_write_some_at_cpo,
               AsyncFile&,
               typename AsyncFile::offset_t,
               BufferSequence>)
          -> tag_invoke_result_t<
              async_write_some_at_cpo,
              AsyncFile&,
              typename AsyncFile::offset_t,
              BufferSequence> {
    return unifex::tag_invoke(
        *this, file, offset, (BufferSequence &&) bufferSequence);
  }
} async_write_some_at{};

UNIFEX_INLINE_VAR constexpr struct open_file_read_only_cpo {
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

UNIFEX_INLINE_VAR constexpr struct open_file_write_only_cpo {
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

UNIFEX_INLINE_VAR constexpr struct open_file_read_write_cpo {
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

using _filesystem::async_read_some_at;
using _filesystem::async_write_some_at;
using _filesystem::open_file_read_only;
using _filesystem::open_file_write_only;
using _filesystem::open_file_read_write;
} // namespace unifex
