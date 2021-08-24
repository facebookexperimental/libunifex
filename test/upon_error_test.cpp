#include <unifex/just.hpp>
#include <unifex/just_error.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/upon_error.hpp>
#include <iostream>

#include <chrono>
#include <iostream>
#include <type_traits>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

TEST(UponError, Working) {
  int err_code = 0;
  try {
    sync_wait(upon_error(just_error(42), [] { return 2; }));
  } catch (int err) {
    err_code = err;
  }
  EXPECT_EQ(err_code, 2);
}

TEST(Pipeable, UponError) {
  int err_code = 0;
  try {
    just_error(42)
      | upon_error([]{ return 2; })
      | sync_wait();
  } catch (int err) {
    err_code = err;
  }
  EXPECT_EQ(err_code, 2);
}

TEST(NotCalled, UponError) {
  int err_code = 0;
  try {
    just(42)
      | upon_error([]{ return 2; })
      | sync_wait();
  } catch (int err) {
    err_code = err;
  }
  EXPECT_EQ(err_code, 0);
}
