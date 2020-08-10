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

#include <iostream>

namespace unifex {

namespace _fifo_support {
// Query to return the fifo context if any. Defaults to a null ptr
// if this is not a fifo context.
struct _get_fifo_context_fn {
    template(typename Entity)
        (requires tag_invocable<_get_fifo_context_fn, Entity>)
    void* operator()(Entity&& e) const
        noexcept(is_nothrow_tag_invocable_v<_get_fifo_context_fn, Entity>) {
        return tag_invoke(_get_fifo_context_fn{}, (Entity&&)e);
    }

    template(typename Entity)
        (requires !tag_invocable<_get_fifo_context_fn, Entity>)
    void* operator()(Entity&& s) const {
        return nullptr;
    }
};

// Ask the receiver to start its work early if this is practical
struct _start_eagerly_fn {
    template(typename Entity)
        (requires tag_invocable<_start_eagerly_fn, Entity>)
    bool operator()(Entity&& e) const
        noexcept(is_nothrow_tag_invocable_v<_get_fifo_context_fn, Entity>) {
        return tag_invoke(_start_eagerly_fn{}, (Entity&&)e);
    }

    template(typename Entity)
        (requires !tag_invocable<_start_eagerly_fn, Entity>)
    bool operator()(Entity&& s) const {
      std::cout << "\tDefault start_eagerly returning false\n";
      return false;
    }
};


} // namespace _fifo_support

inline constexpr _fifo_support::_get_fifo_context_fn get_fifo_context{};
inline constexpr _fifo_support::_start_eagerly_fn start_eagerly{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
