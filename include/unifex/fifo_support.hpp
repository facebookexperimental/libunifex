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

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _fifo_support {
// Query to return the fifo context if any. Defaults to a null ptr
// if this is not a fifo context.
struct _get_fifo_context_fn {
    template(typename Entity)
        (requires tag_invocable<_get_fifo_context_fn, Entity>)
    auto operator()(Entity&& e) const
        noexcept(is_nothrow_tag_invocable_v<_get_fifo_context_fn, Entity>)
        -> uintptr_t {
        return tag_invoke(_get_fifo_context_fn{}, (Entity&&)e);
    }

    template(typename Entity)
        (requires !tag_invocable<_get_fifo_context_fn, Entity>)
    uintptr_t operator()(Entity&& s) const {
        return reinterpret_cast<uintptr_t>(nullptr);
    }
};

} // namespace _fifo_support

inline constexpr _fifo_support::_get_fifo_context_fn get_fifo_context{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
