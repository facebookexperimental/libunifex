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
#include <unifex/async_scope.hpp>
#include <unifex/finally.hpp>
#include <unifex/just.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>

#include <optional>

#include <gtest/gtest.h>

#if !UNIFEX_NO_COROUTINES
#include <unifex/task.hpp>
#endif // !UNIFEX_NO_COROUTINES

using namespace unifex;

namespace {

int global;

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

  void anIntRefAPI(void* context, void (*completed)(void* context, int& result)) {
    // Execute some work asynchronously on some other thread. When its
    // work is finished, pass the result to the callback.
    someScope.detached_spawn_call_on(someThread.get_scheduler(), [=]() noexcept {
      completed(context, global);
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
  {
    auto snd = [this](int a, int b) {
      return create<int>([a, b, this](auto& rec) {
        static_assert(receiver_of<decltype(rec), int>);
        static_assert(!receiver_of<decltype(rec), int*>);
        anIntAPI(a, b, &rec, [](void* context, int result) {
          unifex::void_cast<decltype(rec)>(context).set_value(result);
        });
      });
    }(1, 2);

    std::optional<int> res = sync_wait(std::move(snd));
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 3);
  }

  {
    auto snd = [this]() {
      return create<int&>([this](auto& rec) {
        static_assert(receiver_of<decltype(rec), int&>);
        anIntRefAPI(&rec, [](void* context, int& result) {
          unifex::void_cast<decltype(rec)>(context).set_value(result);
        });
      });
    }();

    std::optional<std::reference_wrapper<int>> res = sync_wait(std::move(snd));
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(&res->get(), &global);
  }
}

TEST_F(CreateTest, FinallyCreate) {
    auto snd = [this](int a, int b) {
      return create<int>([a, b, this](auto& rec) {
        static_assert(receiver_of<std::decay_t<decltype(rec)>, int>);
        anIntAPI(a, b, &rec, [](void* context, int result) {
          unifex::void_cast<decltype(rec)>(context).set_value(result);
        });
      });
    }(1, 2) | finally(just());

    std::optional<int> res = sync_wait(std::move(snd));
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 3);
}

TEST_F(CreateTest, DoubleCreateSetsIntValue) {
  auto snd = [this](int a, int b) {
    return create<double>([a, b, this](auto& rec) {
      static_assert(receiver_of<std::decay_t<decltype(rec)>, int>);
      anIntAPI(a, b, &rec, [](void* context, int result) {
        unifex::void_cast<decltype(rec)>(context).set_value(result);
      });
    });
  }(1, 2);

  static_assert(std::is_same_v<decltype(sync_wait(std::move(snd))), std::optional<double>>);
  std::optional<double> res = sync_wait(std::move(snd));
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 3);
}

struct TrackingObject {
  static int moves;
  static int copies;

  explicit TrackingObject(int val) : val(val) {}
  TrackingObject(const TrackingObject& other) : val(other.val) {
    ++copies;
  }
  TrackingObject(TrackingObject&& other) : val(other.val) {
    ++moves;
    other.was_moved = true;
  }
  TrackingObject& operator=(const TrackingObject&) = delete;
  TrackingObject& operator=(TrackingObject&&) = delete;

  int val;
  bool was_moved = false;
};
int TrackingObject::moves = 0;
int TrackingObject::copies = 0;

TEST_F(CreateTest, CreateObjectNotCopied) {
  auto snd = [this](int a, int b) {
    return create<TrackingObject>([a, b, this](auto& rec) {
      static_assert(receiver_of<std::decay_t<decltype(rec)>, TrackingObject>);
      anIntAPI(a, b, &rec, [](void* context, int result) {
        unifex::void_cast<decltype(rec)>(context).set_value(TrackingObject{result});
      });
    });
  }(1, 2);

  TrackingObject::copies = 0;

  std::optional<TrackingObject> res = sync_wait(std::move(snd));
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->val, 3);
  EXPECT_EQ(TrackingObject::copies, 0);
}

TEST_F(CreateTest, CreateObjectCopied) {
  auto snd = [this](int a, int b) {
    return create<TrackingObject>([a, b, this](auto& rec) {
      static_assert(receiver_of<std::decay_t<decltype(rec)>, TrackingObject>);
      anIntAPI(a, b, &rec, [](void* context, int result) {
        TrackingObject obj{result};
        unifex::void_cast<decltype(rec)>(context).set_value(obj);
      });
    });
  }(1, 2);

  TrackingObject::copies = 0;

  std::optional<TrackingObject> res = sync_wait(std::move(snd));
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->val, 3);
  EXPECT_EQ(TrackingObject::copies, 1);
}

TEST_F(CreateTest, CreateObjectLeadsToNewObject) {
  auto snd = [this](int a, int b) {
    return create<TrackingObject>([a, b, this](auto& rec) {
      static_assert(receiver_of<std::decay_t<decltype(rec)>, TrackingObject>);
      anIntAPI(a, b, &rec, [](void* context, int result) {
        unifex::void_cast<decltype(rec)>(context).set_value(TrackingObject{result});
      });
    });
  }(1, 2) | then([](TrackingObject&& obj) {
      return obj.val;
  });

  TrackingObject::copies = 0;
  TrackingObject::moves = 0;

  std::optional<int> res = sync_wait(std::move(snd));
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 3);
  EXPECT_EQ(TrackingObject::copies, 0);
  EXPECT_GE(TrackingObject::moves, 1);
}

TEST_F(CreateTest, CreateWithConditionalMove) {
  TrackingObject obj{0};

  struct Data {
    void* context;
    TrackingObject* obj;
  };
  Data data{nullptr, &obj};

  auto snd = [this, &data](int a, int b) {
    return create<TrackingObject&&>([a, b, &data, this](auto& rec) {
      static_assert(receiver_of<std::decay_t<decltype(rec)>, TrackingObject&&>);
      data.context = &rec;
      anIntAPI(a, b, &data, [](void* context, int result) {
        Data& data = unifex::void_cast<Data&>(context);
        data.obj->val = result;
        unifex::void_cast<decltype(rec)>(data.context).set_value(std::move(*data.obj));
      });
    });
  }(1, 2) | then([](TrackingObject&& obj) {
      return obj.val;
  });

  TrackingObject::copies = 0;
  TrackingObject::moves = 0;

  std::optional<int> res = sync_wait(std::move(snd));
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 3);
  EXPECT_EQ(TrackingObject::copies, 0);
  EXPECT_EQ(TrackingObject::moves, 0);
  EXPECT_FALSE(obj.was_moved);
}

TEST_F(CreateTest, CreateWithConversions) {
  struct A {
    int val;
  };
  struct B {
    B(A a) : val(a.val) {}
    B(int val) : val(val) {}
    operator A() const {
      return A{val};
    }
    int val;
  };

  {
    auto snd = [this](int a, int b) {
      return create<A>([a, b, this](auto& rec) {
        static_assert(receiver_of<std::decay_t<decltype(rec)>, A>);
        anIntAPI(a, b, &rec, [](void* context, int result) {
          unifex::void_cast<decltype(rec)>(context).set_value(B{result});
        });
      });
    }(1, 2);

    std::optional<A> res = sync_wait(std::move(snd));
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->val, 3);
  }

  {
    auto snd = [this](int a, int b) {
      return create<B>([a, b, this](auto& rec) {
        static_assert(receiver_of<std::decay_t<decltype(rec)>, int>);
        anIntAPI(a, b, &rec, [](void* context, int result) {
          unifex::void_cast<decltype(rec)>(context).set_value(A{result});
        });
      });
    }(1, 2);

    std::optional<B> res = sync_wait(std::move(snd));
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->val, 3);
  }
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
