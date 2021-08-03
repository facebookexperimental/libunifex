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

#include <unifex/tag_invoke.hpp>

#include <gtest/gtest.h>

namespace {
inline constexpr struct test_cpo {} test;

struct X {
    friend void tag_invoke(test_cpo, X) {}
    friend constexpr bool tag_invoke(test_cpo, X, int a) noexcept { return a > 0; }
};

struct Y {};
} // anonymous namespace

// Tests of the tag_invoke metafunctions.
static_assert(std::is_same_v<void, unifex::tag_invoke_result_t<test_cpo, X>>);
static_assert(std::is_same_v<bool, unifex::tag_invoke_result_t<test_cpo, X, int>>);
static_assert(unifex::is_tag_invocable_v<test_cpo, X>);
static_assert(unifex::is_tag_invocable_v<test_cpo, X, int>);
static_assert(!unifex::is_tag_invocable_v<test_cpo, Y>);
static_assert(!unifex::is_nothrow_tag_invocable_v<test_cpo, X>);
static_assert(unifex::is_nothrow_tag_invocable_v<test_cpo, X, int>);
static_assert(!unifex::is_nothrow_tag_invocable_v<test_cpo, Y>);

TEST(tag_invoke, tag_invoke_usage) {
    unifex::tag_invoke(test, X{});
    EXPECT_TRUE(unifex::tag_invoke(test_cpo{}, X{}, 42));
}

TEST(tag_invoke, tag_invoke_constexpr) {
    constexpr bool result1 = unifex::tag_invoke(test, X{}, 42);
    constexpr bool result2 = unifex::tag_invoke(test, X{}, -3);
    EXPECT_TRUE(result1);
    EXPECT_FALSE(result2);
}
