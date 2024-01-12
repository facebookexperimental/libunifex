#include <gtest/gtest.h>

#include <unifex/async_generator.hpp>
#include <unifex/coroutine_concepts.hpp>
#include <unifex/for_each.hpp>
#include <unifex/just.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/reduce_stream.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/transform_stream.hpp>

using namespace unifex;

TEST(async_generator, await_in_async_generator) {
  static unifex::single_thread_context ctx;

  auto outer_tid = std::this_thread::get_id();

  static auto gen5 = [](std::thread::id outer_tid) -> async_generator<int> {
    EXPECT_EQ(outer_tid, std::this_thread::get_id());
    co_await unifex::schedule(ctx.get_scheduler());
    EXPECT_NE(outer_tid, std::this_thread::get_id());
    co_yield 1;
    co_yield co_await unifex::just(2);
    co_yield 3;
    co_yield 4;
    co_yield 5;
  };

  auto result = unifex::sync_wait([](std::thread::id outer_tid) -> task<int> {
    EXPECT_EQ(outer_tid, std::this_thread::get_id());
    auto gen = gen5(outer_tid);
    EXPECT_EQ(outer_tid, std::this_thread::get_id());
    int sum = 0;

    co_await unifex::for_each(
        std::move(gen), [&sum](int el) mutable { sum += el; });
    co_return sum;
  }(outer_tid));
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(15, *result);
}

TEST(async_generator, gen_with_stream_op) {
  static unifex::single_thread_context callback_context;

  auto makeInts = [](int n) -> async_generator<int> {
    co_await unifex::schedule(callback_context.get_scheduler());
    for (int i = 1; i <= n; ++i) {
      co_yield i;
    }
  };

  auto lazyReduced =
      makeInts(4) | unifex::reduce_stream(0, [](int state, int currVal) {
        return state + currVal;
      });
  auto result = unifex::sync_wait(std::move(lazyReduced));
  EXPECT_EQ(10, result);
}

// Resuming the generator happens on the expected scheduler (similar semantics
// to unifex::task<>)
TEST(async_generator, test_gen_affinity) {
  // a bunch of ids, all of which we expect to be the main thread id
  std::vector<std::thread::id> expectedMainThreadId{};
  // a bunch of ids, all of which are expected to be the thread id
  // where the generator coroutine executes
  std::vector<std::thread::id> expectedGenThreadId{};
  // The context where our generator will be executing
  static unifex::single_thread_context genExecutionContext;

  static unifex::single_thread_context innerTaskScheduler;

  auto makeInts = [&](int n) mutable -> async_generator<int> {
    // At very first, we expect to be on the main thread still
    expectedMainThreadId.emplace_back(std::this_thread::get_id());
    co_await unifex::schedule(genExecutionContext.get_scheduler());
    // After re-sched, we expect to be on a different thread
    expectedGenThreadId.emplace_back(std::this_thread::get_id());

    // A task that switches to a new schedule every time.
    // It is called within the iteration below & used to
    // verify we're always shifting back to the generator's
    // scheduler.
    auto innerTask = [&]() -> task<void> {
      co_await unifex::schedule(innerTaskScheduler.get_scheduler());
    };

    for (int i = 1; i <= n; ++i) {
      // Before and after yield / co_await, we expect to be on the gen thread as
      // well
      // -> resuming the generator brings us back to that thread
      expectedGenThreadId.emplace_back(std::this_thread::get_id());
      co_yield i;
      co_await innerTask();
      expectedGenThreadId.emplace_back(std::this_thread::get_id());
    }
  };

  auto result = unifex::sync_wait([&]() mutable -> task<int> {
    auto gen = makeInts(4);
    expectedMainThreadId.emplace_back(std::this_thread::get_id());
    int sum = 0;

    co_await unifex::for_each(
        std::move(gen), [&sum, &expectedMainThreadId](int el) mutable {
          // Before and after we expect to be the same as the main thread
          expectedMainThreadId.emplace_back(std::this_thread::get_id());
          sum += el;
          expectedMainThreadId.emplace_back(std::this_thread::get_id());
        });
    co_return sum;
  }());

  ASSERT_FALSE(expectedMainThreadId.empty());
  ASSERT_FALSE(expectedGenThreadId.empty());

  // All inside expectedMainThreadId are the same & are equal to the main thread
  // id
  EXPECT_TRUE(
      std::equal(
          expectedMainThreadId.begin() + 1,
          expectedMainThreadId.end(),
          expectedMainThreadId.begin()) &&
      expectedMainThreadId.front() == std::this_thread::get_id());

  // All inside expectedGenThreadId are the same
  EXPECT_TRUE(std::equal(
      expectedGenThreadId.begin() + 1,
      expectedGenThreadId.end(),
      expectedGenThreadId.begin()));

  ASSERT_TRUE(result);
  EXPECT_EQ(10, result);
}
