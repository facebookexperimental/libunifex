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

#include <unifex/execute.hpp>
#include <unifex/inline_scheduler.hpp>

#include <gtest/gtest.h>

using namespace unifex;

TEST(Execute, execute_with_scheduler) {
  int i = 0;
  execute(inline_scheduler{}, [&]() { ++i; });
  EXPECT_EQ(1, i);
}

TEST(Execute, Pipeable) {
  int i = 0;
  struct _receiver {
    int *p;
    void set_value() && noexcept {
      *p += 1;
    }
    void set_error(std::exception_ptr) && noexcept {
      *p += 2;
    }
    void set_done() && noexcept {
      *p += 4;
    }
  };
  schedule(inline_scheduler{})
    | submit(_receiver{&i});
  EXPECT_EQ(1, i);
}
