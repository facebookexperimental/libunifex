/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/tag_invoke.hpp>

#include <cstdint>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _socket {
using port_t = std::uint16_t;

inline constexpr struct open_listening_socket_cpo final {
  template <typename Scheduler>
  constexpr auto operator()(Scheduler&& sched, port_t port) const noexcept(
      is_nothrow_tag_invocable_v<open_listening_socket_cpo, Scheduler, port_t>)
      -> tag_invoke_result_t<open_listening_socket_cpo, Scheduler, port_t> {
    return tag_invoke(*this, static_cast<Scheduler&&>(sched), port);
  }
} open_listening_socket{};
}  // namespace _socket

using _socket::open_listening_socket;
using _socket::port_t;
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
