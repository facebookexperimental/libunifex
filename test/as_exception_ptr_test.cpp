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

#include <unifex/as_exception_ptr.hpp>
#include <unifex/receiver_concepts.hpp>

#include <gtest/gtest.h>

using namespace unifex;

TEST(as_exception_ptr, error_code) {
  try {
    std::rethrow_exception(
        as_exception_ptr(std::make_error_code(std::errc::not_supported)));
  } catch (std::system_error& ex) {
    EXPECT_EQ(ex.code(), std::errc::not_supported);
  }
}

struct test_error {
  int error_code;

  friend std::exception_ptr
  tag_invoke(tag_t<as_exception_ptr>, test_error&& error) noexcept {
    return std::make_exception_ptr(std::runtime_error(
        std::to_string(std::forward<test_error>(error).error_code)));
  }
};

TEST(as_exception_ptr, custom_error) {
  try {
    std::rethrow_exception(as_exception_ptr(test_error{42}));
  } catch (std::runtime_error& ex) {
    EXPECT_EQ(ex.what(), std::string_view{"42"});
  }
}

struct error_code_receiver {
  std::optional<std::error_code>& ec_;
  void set_error(std::error_code ec) && noexcept {
    ec_ = (std::error_code &&) ec;
  }
};

struct exception_ptr_receiver {
  std::optional<std::exception_ptr>& ex_;
  void set_error(std::exception_ptr ex) && noexcept {
    ex_ = (std::exception_ptr &&) ex;
  }
};

TEST(as_exception_ptr, set_error) {
  { // direct call with error_code
    std::optional<std::error_code> ec{};
    unifex::set_error(
        error_code_receiver{ec},
        std::make_error_code(std::errc::not_supported));
    EXPECT_TRUE(ec.has_value());
  }
  { // direct call with exception_ptr
    std::optional<std::exception_ptr> ex{};
    auto eptr = std::make_exception_ptr(
        std::system_error{std::make_error_code(std::errc::not_supported)});
    unifex::set_error(exception_ptr_receiver{ex}, std::move(eptr));
    EXPECT_TRUE(ex.has_value());
  }
  {  // call through as_exception_ptr CPO
    std::optional<std::exception_ptr> ex{};
    unifex::set_error(
        exception_ptr_receiver{ex},
        std::make_error_code(std::errc::not_supported));
    EXPECT_TRUE(ex.has_value());
  }
}
