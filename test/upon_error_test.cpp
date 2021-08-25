#include <unifex/just.hpp>
#include <unifex/just_error.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/upon_error.hpp>

#include <type_traits>

#include <gtest/gtest.h>

using namespace unifex;

TEST(UponError, Working) {
  int val = 0;
  auto res = sync_wait(upon_error(just_error(42), [&](auto err_val) {
    val = err_val;
    return 2;
  }));
  EXPECT_EQ(val, 42);
  EXPECT_EQ(res.value(), 2);
}

TEST(Pipeable, UponError) {
  int val = 0;
  auto res = just_error(42) 
    | upon_error([&](auto err_val) {
        val = err_val;
        return 2;
      })
    | sync_wait();
  EXPECT_EQ(val, 42);
  EXPECT_EQ(res.value(), 2);
}

TEST(NotCalled, UponError) {
  int val = 0;
  auto res = just(42)
    | upon_error([&](auto) {
      val = 1;
      return 2;
    })
    | sync_wait();
  EXPECT_EQ(val, 0);
  EXPECT_EQ(res.value(), 42);
}

TEST(ExceptionHandling, UponError) {
  int val = 0;
  try{
    just(42)
      | upon_error([&](auto) {
        val = 1;
        throw 2;
        return 2;
      })
      | sync_wait();
  } catch(int err){
    EXPECT_EQ(err, 2);
  }
  EXPECT_EQ(val, 0);
}

TEST(VoidReturn, UponError) {
  int val = 0;
  just_error(42) 
    | upon_error([&](auto){
        val = 2;
      })
    | sync_wait();
  EXPECT_EQ(val, 2);
}
