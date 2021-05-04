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

#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES

#include <unifex/at_coroutine_exit.hpp>

#include <unifex/task.hpp>
#include <unifex/sync_wait.hpp>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
int global = 0;
task<void> test1() {
  global = 2;
  co_await at_coroutine_exit([]() -> task<void> { global *= 2; co_return; });
}
}

TEST(AtCoroutineExit, SimpleAction) {
  sync_wait(test1());
  EXPECT_EQ(global, 4);
}

#endif // !UNIFEX_NO_COROUTINES
