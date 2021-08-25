#include <unifex/just.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/just_done.hpp>
#include <unifex/upon_done.hpp>

#include <gtest/gtest.h>

using namespace unifex;

TEST(UponDone, Working) {
  int count = 0;
  sync_wait(upon_done(just_done(), [&] { ++count; }));
  EXPECT_EQ(count, 1);
}

TEST(UponDone, Pipeable){
  int count = 0;

  just_done()
    | upon_done([&]{count++;})
    | sync_wait();

  just_done()
    | upon_done([&]{count++;})
    | sync_wait();

  EXPECT_EQ(count, 2);
}

TEST(UponDone, NotCalled){
  int count = 0;

  auto x = just(42)
    | upon_done([&]{count++;})
    | sync_wait();

  EXPECT_EQ(count, 0);
  EXPECT_EQ(x.value(), 42);
}

TEST(UponDone, ReturningValue) {
  int count = 0;
  auto res = just_done()
    | upon_done([&]{
        count++;
        return 42;
        })
    | sync_wait();
  EXPECT_EQ(count, 1);
  EXPECT_EQ(res.value(), 42);
}

TEST(UponDone, VoidReturnCallback) {
  int count = 0;
  auto res = just(32)
    | upon_done([&]{
        count++;
      })
    | sync_wait();
  EXPECT_EQ(count, 0);
  EXPECT_EQ(res.value(), 32);
}
