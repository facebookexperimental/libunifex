#include <unifex/filter_stream.hpp>

#include <unifex/for_each.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/reduce_stream.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform_stream.hpp>

#include <gtest/gtest.h>

using namespace unifex;

TEST(filter_stream, StepByStep) {
  auto ints = range_stream{1, 11};
  auto evens =
      filter_stream(std::move(ints), [](int val) { return val % 2 == 0; });

  auto sum = reduce_stream(
      std::move(evens), 0, [](int state, int val) { return state + val; });

  auto res = (sync_wait(std::move(sum)));
  ASSERT_TRUE(res);
  EXPECT_EQ(30, *res);
}

TEST(filter_stream, Composition) {
  auto res = sync_wait(reduce_stream(
      filter_stream(range_stream{1, 11}, [](int val) { return val % 2 == 0; }),
      0,
      [](int state, int val) { return state + val; }));

  ASSERT_TRUE(res);
  EXPECT_EQ(30, *res);
}

TEST(filter_stream, Pipeable) {
  auto res = range_stream{1, 11} |
      filter_stream([](int val) { return val % 2 == 0; }) |
      reduce_stream(0, [](int state, int val) { return state + val; }) |
      sync_wait();

  ASSERT_TRUE(res);
  EXPECT_EQ(30, *res);
}

TEST(filter_stream, TransformThrows) {
  range_stream{1, 11} | transform_stream([](int val) {
    if (val % 2 == 0) {
      throw;
    }
    return val * 2;
  }) | for_each([](int el) { std::cout << "el=" << el << std::endl; }) |
      sync_wait();

  // ASSERT_TRUE(res);
  // EXPECT_EQ(30, *res);
}
