#pragma once

#include <unifex/tag_invoke.hpp>

#include <unifex/filesystem.hpp>

namespace unifex {

inline constexpr struct async_read_some_at_cpo {
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
} async_read_some_at;

inline constexpr struct async_write_some_at_cpo {
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
} async_write_some_at;

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
} open_file_read_only;

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
} open_file_write_only;

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
} open_file_read_write;

} // namespace unifex
