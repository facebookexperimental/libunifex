/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this socket except in compliance with the License.
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

// The senders would be loosened eventually to support sequences.
//
// // make async_read_some act like blocking read()
// auto bytes_read = sync_wait(
//     select_first_arg(
//         async_read_some(socket)(just(buffer)))).size();
//
// // make async_write_some act like blocking write()
// auto bytes_written = sync_wait(
//     select_first_arg(
//         async_write_some(socket)(just(buffer)))).size();
//
// // echo
// sync_wait(
//     repeat(async_write_some(socket)(
//         select_first_arg(
//             async_read_some(socket)(just(buffer))))));
//
// // echo with pipe
// just(buffer) |
//     async_read_some(socket) |
//     select_first_arg() |
//     async_write_some(socket) |
//     repeat() |
//     sync_wait();
//

namespace unifex {
namespace _io_cpo {
//
// async_read_some
//
// returns a function that takes a sender of BufferSequence
// which returns a sender of 2 BufferSequence. One that has been truncated
// to only describe the filled portions of the original BufferSequence
// Another that only describes the remaining portions of the original
// BufferSequence
//
inline constexpr struct async_read_some_cpo {
  template <typename ForwardReader, typename BufferSequence>
  auto operator()(
      ForwardReader& socket,
      BufferSequence&& bufferSequence) const
      noexcept(is_nothrow_tag_invocable_v<
               async_read_some_cpo,
               ForwardReader&,
               BufferSequence>)
          -> tag_invoke_result_t<
              async_read_some_cpo,
              ForwardReader&,
              BufferSequence> {
    return unifex::tag_invoke(
        *this, socket, (BufferSequence &&) bufferSequence);
  }

  template <typename ForwardReader>
  auto operator()(
      ForwardReader& socket) const
      noexcept(is_nothrow_tag_invocable_v<
               async_read_some_cpo,
               ForwardReader&>)
          -> tag_invoke_result_t<
              async_read_some_cpo,
              ForwardReader&> {
    return unifex::tag_invoke(*this, socket);
  }
} async_read_some{};

// async_write_some
//
// returns a function that takes a sender of BufferSequence
// which returns a sender of 2 BufferSequence. One that has been truncated
// to only describe the written portions of the original BufferSequence
// Another that only describes the remaining portions of the original
// BufferSequence
//
inline constexpr struct async_write_some_cpo {
  template <typename ForwardWriter, typename BufferSequence>
  auto operator()(
      ForwardWriter& socket,
      BufferSequence&& bufferSequence) const
      noexcept(is_nothrow_tag_invocable_v<
               async_write_some_cpo,
               ForwardWriter&,
               BufferSequence>)
          -> tag_invoke_result_t<
              async_write_some_cpo,
              ForwardWriter&,
              BufferSequence> {
    return unifex::tag_invoke(
        *this, socket, (BufferSequence &&) bufferSequence);
  }

  template <typename ForwardWriter>
  auto operator()(
      ForwardWriter& socket) const
      noexcept(is_nothrow_tag_invocable_v<
               async_write_some_cpo,
               ForwardWriter&>)
          -> tag_invoke_result_t<
              async_write_some_cpo,
              ForwardWriter&> {
    return unifex::tag_invoke(
        *this, socket);
  }
} async_write_some{};

inline constexpr struct async_read_some_at_cpo {
  template <typename RandomReader, typename BufferSequence>
  auto operator()(
      RandomReader& file,
      typename RandomReader::offset_t offset,
      BufferSequence&& bufferSequence) const
      noexcept(is_nothrow_tag_invocable_v<
               async_read_some_at_cpo,
               RandomReader&,
               typename RandomReader::offset_t,
               BufferSequence>)
          -> tag_invoke_result_t<
              async_read_some_at_cpo,
              RandomReader&,
              typename RandomReader::offset_t,
              BufferSequence> {
    return unifex::tag_invoke(
        *this, file, offset, (BufferSequence &&) bufferSequence);
  }

  template <typename RandomReader>
  auto operator()(
      RandomReader& file) const
      noexcept(is_nothrow_tag_invocable_v<
               async_read_some_at_cpo,
               RandomReader&>)
          -> tag_invoke_result_t<
              async_read_some_at_cpo,
              RandomReader&> {
    return unifex::tag_invoke(
        *this, file);
  }
} async_read_some_at{};

inline constexpr struct async_write_some_at_cpo {
  template <typename RandomWriter, typename BufferSequence>
  auto operator()(
      RandomWriter& file,
      typename RandomWriter::offset_t offset,
      BufferSequence&& bufferSequence) const
      noexcept(is_nothrow_tag_invocable_v<
               async_write_some_at_cpo,
               RandomWriter&,
               typename RandomWriter::offset_t,
               BufferSequence>)
          -> tag_invoke_result_t<
              async_write_some_at_cpo,
              RandomWriter&,
              typename RandomWriter::offset_t,
              BufferSequence> {
    return unifex::tag_invoke(
        *this, file, offset, (BufferSequence &&) bufferSequence);
  }

  template <typename RandomWriter>
  auto operator()(
      RandomWriter& file) const
      noexcept(is_nothrow_tag_invocable_v<
               async_write_some_at_cpo,
               RandomWriter&>)
          -> tag_invoke_result_t<
              async_write_some_at_cpo,
              RandomWriter&> {
    return unifex::tag_invoke(
        *this, file);
  }
} async_write_some_at{};
} // namespace _io_cpo

using _io_cpo::async_read_some;
using _io_cpo::async_write_some;
using _io_cpo::async_read_some_at;
using _io_cpo::async_write_some_at;

} // namespace unifex
