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

#include <unifex/execution_policy.hpp>
#include <unifex/tag_invoke.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
    namespace _get_execution_policy {
        struct _fn {
            template(typename PolicyProvider)
                (requires tag_invocable<_fn, const PolicyProvider&>)
            constexpr auto operator()(const PolicyProvider& provider) const noexcept
                -> tag_invoke_result_t<_fn, const PolicyProvider&> {
                return tag_invoke(_fn{}, provider);
            }

            template(typename PolicyProvider)
                (requires (!tag_invocable<_fn, const PolicyProvider&>))
            constexpr sequenced_policy operator()([[maybe_unused]] const PolicyProvider&) const noexcept {
                return {};
            }
        };
    }

    inline constexpr _get_execution_policy::_fn get_execution_policy{};
}

#include <unifex/detail/epilogue.hpp>
