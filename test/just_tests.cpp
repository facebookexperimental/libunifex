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
#include <unifex/just.hpp>

#include <unifex/blocking.hpp>
#include <unifex/sender_concepts.hpp>

#include <gtest/gtest.h>

using unifex::blocking_kind;
using unifex::just;
using unifex::sender_traits;

namespace {

TEST(just_tests, just_void) {
  using just_t = decltype(just());

  static_assert(blocking_kind::always_inline == sender_traits<just_t>::blocking);
  static_assert(sender_traits<just_t>::is_always_scheduler_affine);
}

}  // namespace
