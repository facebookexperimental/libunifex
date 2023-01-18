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

#include <unifex/create.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/async_scope.hpp>
#include <unifex/sync_wait.hpp>

#include <optional>

#include <gtest/gtest.h>

#if !UNIFEX_NO_COROUTINES
#include <unifex/task.hpp>
#endif // !UNIFEX_NO_COROUTINES

using namespace unifex;

namespace {
struct CreateTest : testing::Test {
  unifex::single_thread_context someThread;
  unifex::async_scope someScope;

  ~CreateTest() {
    sync_wait(someScope.cleanup());
  }

  void anIntAPI(int a, int b, void* context, void (*completed)(void* context, int result)) {
    // Execute some work asynchronously on some other thread. When its
    // work is finished, pass the result to the callback.
    someScope.detached_spawn_call_on(someThread.get_scheduler(), [=]() noexcept {
      auto result = a + b;
      completed(context, result);
    });
  }

  void aVoidAPI(void* context, void (*completed)(void* context)) {
    // Execute some work asynchronously on some other thread. When its
    // work is finished, pass the result to the callback.
    someScope.detached_spawn_call_on(someThread.get_scheduler(), [=]() noexcept {
      completed(context);
    });
  }
};
} // anonymous namespace

TEST_F(CreateTest, BasicTest) {
  auto snd = [this](int a, int b) {
    return create<int>([a, b, this](auto& rec) {
      static_assert(receiver_of<decltype(rec), int>);
      anIntAPI(a, b, &rec, [](void* context, int result) {
        unifex::void_cast<decltype(rec)>(context).set_value(result);
      });
    });
  }(1, 2);

  std::optional<int> res = sync_wait(std::move(snd));
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 3);
}

TEST_F(CreateTest, VoidWithContextTest) {
  bool called = false;
  auto snd = [&called, this]() {
    return create<>([this](auto& rec) {
      static_assert(receiver_of<decltype(rec)>);
      aVoidAPI(&rec, [](void* context) {
        auto& rec2 = unifex::void_cast<decltype(rec)>(context);
        rec2.context().get() = true;
        rec2.set_value();
      });
    },
    std::ref(called));
  }();

  std::optional<unit> res = sync_wait(std::move(snd));
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(called);
}

#if !UNIFEX_NO_COROUTINES

TEST_F(CreateTest, AwaitTest) {
  auto tsk = [](int a, int b, auto self) -> task<int> {
    co_return co_await create<int>([a, b, self](auto& rec) {
      self->anIntAPI(a, b, &rec, [](void* context, int result) {
        unifex::void_cast<decltype(rec)>(context).set_value(result);
      });
    });
  }(1, 2, this);
  std::optional<int> res = sync_wait(std::move(tsk));
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 3);
}

#endif // !UNIFEX_NO_COROUTINES
