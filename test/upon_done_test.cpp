#include <unifex/sync_wait.hpp>
#include <unifex/just_done.hpp>
#include <unifex/upon_done.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

TEST(UponDone, Working) {
  int count = 0;
  sync_wait(upon_done(just_done(), [&] { ++count; }));
  EXPECT_EQ(count, 1);
}

TEST(Pipeable, UponDone){
  int count = 0;

  just_done()
    | upon_done([&]{count++;})
    | sync_wait();

  just_done()
    | upon_done([&]{count++;})
    | sync_wait();

  EXPECT_EQ(count, 2);
}
