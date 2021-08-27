#include <unifex/when_all.hpp>
#include <unifex/then.hpp>
#include <unifex/just.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/just_done.hpp>
#include <unifex/upon_done.hpp>
#include "unifex/let_done.hpp"
#include "unifex/timed_single_thread_context.hpp"

#include <gtest/gtest.h>
#include <type_traits>
#include <variant>
#include <chrono>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

TEST(UponDone, StaticTypeCheck) {
  auto res1 = just(42)
    | upon_done([]{
        return 2;
      });
  static_assert(std::is_same_v<decltype(res1)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>>>);

  auto res2 = just()
    | upon_done([]{
        return 2;
      });
  static_assert(std::is_same_v<decltype(res2)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<>>>);

  auto res3 = just(42)
    | upon_done([]{
        return;
      });
  static_assert(std::is_same_v<decltype(res3)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>>>);

  auto res4 = just(42)
    | upon_done([]{
        return 2.0;
      });
  static_assert(std::is_same_v<decltype(res4)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>>>);

  auto res5 = just_done()
    | upon_done([]{
        return 2;
      });
  static_assert(std::is_same_v<decltype(res5)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>>>);

  auto res6 = just_done()
    | upon_done([]{
      });
  static_assert(std::is_same_v<decltype(res6)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<>>>);

  timed_single_thread_context context;
  auto scheduler = context.get_scheduler();
  auto sch_then_sender = schedule_after(scheduler, 200ms)
    | then([]{return 2;});
  static_assert(std::is_same_v<decltype(sch_then_sender)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>>>);

  auto res7 = sch_then_sender
    | upon_done([]{});
  static_assert(std::is_same_v<decltype(res7)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>, std::tuple<>>>);

  auto res8 = std::move(sch_then_sender)
    | upon_done([]{ return 1.2; });
  static_assert(std::is_same_v<decltype(res8)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>, std::tuple<double>>>);
}

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
    | upon_done([&]{count++; return 2;})
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

TEST(UponDone, NotCalledWithDifferentReturnType){
  int count = 0;
  auto res = just(42)
    | upon_done([&]{
        count++;
        return 2.0;
      })
    | sync_wait();
  EXPECT_EQ(count, 0);
  EXPECT_EQ(res.value(), 42);
}
