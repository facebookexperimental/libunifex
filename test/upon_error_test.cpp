#include <unifex/just.hpp>
#include <unifex/just_error.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/upon_error.hpp>

#include <gtest/gtest.h>
#include <type_traits>
#include <variant>

using namespace unifex;

TEST(UponError, StaticTypeCheck) {
  auto res1 = just(42)
    | upon_error([](auto){return 42;});
  static_assert(std::is_same_v<decltype(res1)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>>>);

  auto res2 = just()
    | upon_error([](auto){
        return 2;
      });
  static_assert(std::is_same_v<decltype(res2)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<>, std::tuple<int>>>);

  auto res3 = just(42)
    | upon_error([](auto){
        return;
      });
  static_assert(std::is_same_v<decltype(res3)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>, std::tuple<>>>);

  auto res4 = just(42)
    | upon_error([](auto){
        return 2.0;
      });
  static_assert(std::is_same_v<decltype(res4)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>, std::tuple<double>>>);
}

TEST(UponError, Working) {
  int val = 0;
  auto res = sync_wait(upon_error(just_error(42), [&](auto err_val) {
    val = err_val;
    return 2;
  }));
  EXPECT_EQ(val, 42);
  EXPECT_EQ(res.value(), 2);
}

TEST(UponError, Pipeable) {
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

TEST(UponError, NotCalled) {
  int val = 0;
  auto res = just(42)
    | upon_error([&](auto) {
      val++;
      return 2;
    })
    | sync_wait();
  EXPECT_EQ(val, 0);
  EXPECT_EQ(res.value(), 42);
}

TEST(UponError, ExceptionHandling) {
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
