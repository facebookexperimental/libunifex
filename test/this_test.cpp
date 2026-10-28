/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the license found in the
 * LICENSE.txt file in the root directory of this source tree.
 */

#include <unifex/this.hpp>

#include <type_traits>

namespace {
struct concrete_type {};

static_assert(std::is_same_v<
              unifex::replace_this_t<const unifex::this_&&, concrete_type>,
              const concrete_type&&>);
}  // namespace
