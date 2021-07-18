/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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
#include <unifex/for_each.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/transform_stream.hpp>

#include <cstdio>
#include <numeric>

#include <gtest/gtest.h>

using namespace unifex;

TEST(range_stream, iota) {
  range_stream{std::views::iota(0, 10)}
    | transform_stream([](int value) { return value * value; })
    | for_each([](int value) { std::printf("got %i\n", value); })
    | transform([]() { std::printf("done\n"); })
    | sync_wait();
}

TEST(range_stream, iota_vector) {
  std::vector v{10};
  std::iota(std::begin(v), std::end(v), 0);

  range_stream{v}
  | transform_stream([](int value) { return value * value; })
  | for_each([](int value) { std::printf("got %i\n", value); })
  | transform([]() { std::printf("done\n"); })
  | sync_wait();
}
TEST(range_stream, rvalue_array) {
  range_stream{std::array{"foo", "bar", "baz"}}
  | for_each([](std::string_view value) { std::printf("got %s\n", value.data()); })
  | transform([]() { std::printf("done\n"); })
  | sync_wait();
}
TEST(range_stream, lvalue_array) {
  constexpr std::array words = {"foo", "bar", "baz"};

  range_stream{words}
  | for_each([](std::string_view value) { std::printf("got %s\n", value.data()); })
  | transform([]() { std::printf("done\n"); })
  | sync_wait();
}
