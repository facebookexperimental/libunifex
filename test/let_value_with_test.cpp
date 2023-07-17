#include <unifex/let_value_with.hpp>

#include <unifex/just.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <chrono>
#include <iostream>
#include <optional>
#include <variant>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
constexpr auto async = [](auto& context, auto&& func) {
    return then(
        schedule_after(context.get_scheduler(), std::chrono::milliseconds(10)),
        (decltype(func))func);
};
}

TEST(LetValueWith, StatefulFactory) {
  // Verifies the let_value_with operation state holds onto the
  // Factory object until the operation is complete.
  timed_single_thread_context context;
  std::optional<int> result = sync_wait(
      let_value(just(), [&] {
        return let_value_with([x = std::make_unique<int>(10)]() -> int* { return x.get(); }, [&](int*& v) {
            return async(context, [&v]() { return *v; });
          });
      })
    );
  ASSERT_TRUE(!!result);
  EXPECT_EQ(*result, 10);
}

TEST(LetValueWith, CallOperatorRvalueRefOverload) {
  struct Factory {
    int operator()() && {
      return 10;
    }
  };
  std::optional<int> result = sync_wait(let_value_with(Factory{}, [&](int& v) {
      return just(v);
    }));
  ASSERT_TRUE(!!result);
  EXPECT_EQ(*result, 10);
}
