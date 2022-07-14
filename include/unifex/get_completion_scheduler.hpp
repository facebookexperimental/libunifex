/*
 * Copyright (c) Rishabh Dwivedi <rishabhdwivedi17@gmail.com>
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

#include <unifex/config.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/tag_invoke.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _get_completion_scheduler {

template <typename CPO>
struct _fn;

template <>
struct _fn<decltype(set_value)> {
  template (typename Sender)
  (requires sender<Sender> AND
            is_nothrow_tag_invocable_v<_fn, Sender const&> AND
            scheduler<tag_invoke_result_t<_fn, Sender const&>>)
  constexpr auto operator()(const Sender& s) const noexcept {
    return tag_invoke(*this, s);
  }
};

template <>
struct _fn<decltype(set_error)> {
  template (typename Sender)
  (requires sender<Sender> AND
            is_nothrow_tag_invocable_v<_fn, Sender const&> AND
            scheduler<tag_invoke_result_t<_fn, Sender const&>>)
  constexpr auto operator()(Sender const& sender) const noexcept
      -> tag_invoke_result_t<_fn, Sender const&> {
    return tag_invoke(*this, sender);
  }
};

template <>
struct _fn<decltype(set_done)> {
  template (typename Sender)
  (requires sender<Sender> AND
            is_nothrow_tag_invocable_v<_fn, Sender const&> AND
            scheduler<tag_invoke_result_t<_fn, Sender const&>>)
  constexpr auto operator()(Sender const& sender) const noexcept
      -> tag_invoke_result_t<_fn, Sender const&> {
    return tag_invoke(*this, sender);
  }
};

}  // namespace _get_completion_scheduler

template <typename CPO>
inline _get_completion_scheduler::_fn<CPO> get_completion_scheduler{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
