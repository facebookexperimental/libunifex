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

#include <cstdint>

namespace unifex {

// By default the UNIFEX_READ_RETURN_ADDRESS macro is defined to
// unifex::instruction_ptr::read_return_address().  This header defines a mock
// interface that can be used to mock out the return address for testing
// purposes.
//
// Example usage:
// (file: some_test.cpp)
//
// #include <unifex/let_value.hpp>
//
// TEST(Let, ReturnAddress) {
// //set the mock return address to a known value before the test.
//   unifex::mock_instruction_ptr::mock_return_address = 0xdeadc0de;
//   auto lv = unifex::let_value(unifex::just(42), [](int) {
//     return unifex::allocate(unifex::just_done());
//   });
//   EXPECT_EQ(unifex::get_return_address(lv), 0xdeadc0de);
//   EXPECT_NO_THROW(sync_wait(lv));
// }
//
struct mock_instruction_ptr {
  static uintptr_t mock_return_address;
  static instruction_ptr read_return_address() noexcept {
    return instruction_ptr{(void*)mock_return_address};
  }
};

}  // namespace unifex

#define UNIFEX_READ_RETURN_ADDRESS mock_instruction_ptr::read_return_address
