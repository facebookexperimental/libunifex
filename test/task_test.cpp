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

#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES


#include <unifex/just_from.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>

#include <gtest/gtest.h>

using namespace unifex;

int global;

template <class Expected, class Actual>
void check_is_type(Actual&&) {
    static_assert(std::is_same_v<Expected, Actual>);
}

task<int&> await_reference_awaitable() {
  struct AwaitableGlobalRef {
    bool await_ready() noexcept { return true; }
    void await_suspend(coro::coroutine_handle<>) noexcept {};
    int& await_resume() noexcept { return global; }
  };
  check_is_type<int&>(co_await AwaitableGlobalRef{});
  int& x = co_await AwaitableGlobalRef{};
  co_return x;
}

task<int&> await_reference_sender() {
  int& x = co_await just_from([]() -> int& { return global; });
  co_return x;
}

TEST(Task, AwaitAwaitableReturningReference) {
  static_assert(std::is_same_v<
    sender_value_types_t<decltype(await_reference_awaitable()), std::variant, std::tuple>,
    std::variant<std::tuple<int&>>
  >);

  int& ref = sync_wait(await_reference_awaitable()).value();
  static_assert(std::is_same_v<
    std::optional<std::reference_wrapper<int>>,
    decltype(sync_wait(await_reference_awaitable()))>);
  EXPECT_EQ(&ref, &global);
}

TEST(Task, AwaitSenderReturningReference) {
  int& ref = sync_wait(await_reference_sender()).value();
  static_assert(std::is_same_v<
    std::optional<std::reference_wrapper<int>>,
    decltype(sync_wait(await_reference_sender()))>);
  EXPECT_EQ(&ref, &global);
}

#endif // !UNIFEX_NO_COROUTINES
