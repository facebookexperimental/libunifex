#include <unifex/filter_stream.hpp>

#include <unifex/for_each.hpp>
#include <unifex/just_void_or_done.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/reduce_stream.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/trampoline_scheduler.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/via_stream.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

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

TEST(filter_stream, FilterFuncThrows) {
  auto st = range_stream{1, 11} | filter_stream([](int) -> bool { throw 42; });
  EXPECT_THROW(sync_wait(next(st)), int);
}

struct ThrowingStream {
  auto next() {
    // Throw in the 2nd iteration
    if (++i == 2) {
      throw 42;
    }
    return unifex::next(underlyingStream_);
  }

  auto cleanup() { return unifex::cleanup(underlyingStream_); }

  range_stream underlyingStream_{1, 10};
  size_t i = 0;
};

TEST(filter_stream, StreamNextSenderThrows) {
  auto st = ThrowingStream{} | filter_stream([](auto&&) { return true; });

  // first iteration doesn't throw
  EXPECT_EQ(1, sync_wait(next(st)));

  // second iteration throws
  EXPECT_THROW(sync_wait(next(st)), int);
}

// I tried to use "mock_receiver.hpp", but it seems gmock/gmock.h
// isn't available in my set up. Below is a simple way to verify
// set_error() is being called
struct ThrowingReceiver {
  template <typename V>
  void set_value(V&&) {
    throw 42;
  }

  void set_done() noexcept {}

  template <typename E>
  void set_error(E&&) noexcept {
    errorCalled_ = true;
  }

  bool& errorCalled_;
};

TEST(filter_stream, ConnectedReceiverThrowsOnSetValue) {
  auto st = range_stream{1, 11} |
      filter_stream([](int val) -> bool { return val % 2 == 0; });
  auto nextSender = next(st);

  bool errorCalled = false;

  auto rec = ThrowingReceiver{errorCalled};
  auto op = unifex::connect(nextSender, rec);
  unifex::start(op);

  EXPECT_TRUE(errorCalled);
}

struct StreamOfMoveOnlyObjects {
  StreamOfMoveOnlyObjects() {
    pointers_.emplace_back(std::make_unique<int>(1));
    pointers_.emplace_back(nullptr);
    pointers_.emplace_back(nullptr);
    pointers_.emplace_back(std::make_unique<int>(2));
  }

  auto next() {
    return just_void_or_done(curr_ < pointers_.size()) |
        then([this]() mutable noexcept {
             return std::move(pointers_[curr_++]);
           });
  }

  auto cleanup() { return just_done(); }

  std::vector<std::unique_ptr<int>> pointers_{};
  size_t curr_ = 0;
};

TEST(filter_stream, MoveOnlyObjects) {
  auto sumOfNonNulls = StreamOfMoveOnlyObjects{} |
      filter_stream([](auto&& ptr) { return ptr != nullptr; }) |
      reduce_stream(0, [](int state, auto&& ptr) { return state + *ptr; }) |
      sync_wait();

  ASSERT_TRUE(sumOfNonNulls);
  EXPECT_EQ(3, *sumOfNonNulls);
}

TEST(filter_stream, StreamOfReferences) {
  std::array<int, 5> ints{1, 2, 3, 4, 5};

  auto res = range_stream{0, 4} |
      transform_stream([&](int idx) -> int& { return ints[idx]; }) |
      filter_stream([](int val) { return val % 2 == 0; }) |
      transform_stream([&](int& val) {
               // ensuring we're propagating the referenceness correctly
               if (val == 2) {
                 EXPECT_TRUE(&val == &ints[1]);
               } else if (val == 4) {
                 EXPECT_TRUE(&val == &ints[3]);
               }
               return val;
             }) |
      reduce_stream(0, [](int state, int val) { return state + val; }) |
      sync_wait();

  ASSERT_TRUE(res);
  EXPECT_EQ(6, *res);
}

TEST(filter_stream, StackExhaustion) {
  auto res = range_stream{1, 100'000} | via_stream(trampoline_scheduler{}) |
      filter_stream([](int) { return false; }) |
      reduce_stream(0, [](int state, int val) { return state + val; }) |
      sync_wait();

  ASSERT_TRUE(res);
  EXPECT_EQ(0, *res);
}
