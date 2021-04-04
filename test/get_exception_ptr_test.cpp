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

#include <unifex/get_exception_ptr.hpp>

#include <gtest/gtest.h>

using namespace unifex;

namespace unifex
{
  std::exception_ptr
  tag_invoke(tag_t<unifex::get_exception_ptr>, std::error_code error) {
    return std::make_exception_ptr(std::system_error(std::move(error)));
  }
}  // namespace unifex

static_assert(is_exception_ptr_convertible_v<std::error_code>);

TEST(get_exception_ptr, error_code) {
  try {
    auto ec = std::make_error_code(std::errc::not_supported);
    std::rethrow_exception(
        get_exception_ptr(std::move(ec)));
  } catch (std::system_error& ex) {
    EXPECT_EQ(ex.code(), std::errc::not_supported);
  }
}

struct test_error {
  int error_code;
};

std::exception_ptr tag_invoke(tag_t<get_exception_ptr>, test_error&& error) {
  return std::make_exception_ptr(
      std::runtime_error(std::to_string(error.error_code)));
}

static_assert(is_exception_ptr_convertible_v<test_error>);

TEST(get_exception_ptr, custom_error) {
  try {
    std::rethrow_exception(get_exception_ptr(test_error{42}));
  } catch (std::runtime_error& ex) {
    EXPECT_EQ(ex.what(), std::string_view{"42"});
  }
}
