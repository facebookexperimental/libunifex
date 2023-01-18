/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>
#include <unifex/allocate.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>

#include <chrono>
#include <iostream>
#include <optional>
#include <variant>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {
constexpr auto async = [](auto& context, auto&& func) {
    return then(
        schedule_after(context.get_scheduler(), 100ms),
        (decltype(func))func);
};

constexpr auto asyncVector = [](auto& context) {
    return async(context, [] {
        std::cout << "producing vector" << std::endl;
        return std::vector<int>{1, 2, 3, 4};
    });
};

namespace _never_block {
template <typename... Values>
struct sender {
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<Values...>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = false;

  friend constexpr auto tag_invoke(tag_t<blocking>, const sender&) noexcept {
    return blocking_kind::never;
  }
};

inline const struct _fn {
  template <typename... Values>
  constexpr auto operator()(Values&&... values) const noexcept {
    return _never_block::sender{(Values&&)values...};
  }

} never_block{};
} // namespace _never_block

using _never_block::never_block;

namespace _multi {
struct _multi_sender {
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<int>, Tuple<double>>;
  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;
  static constexpr bool sends_done = false;
};

inline const struct _fn {
  template <typename... Values>
  constexpr auto operator()(Values&&...) const noexcept {
    return _multi::_multi_sender{};
  }
} multi_sender{};
} // namespace _multi
using _multi::multi_sender;
} // anonymous namespace

TEST(Let, Simple) {
  timed_single_thread_context context;

  // Simple usage of 'let_value()'
  // - defines an async scope in which the result of one async
  //   operation is in-scope for the duration of a second operation.
  std::optional<int> result =
      sync_wait(let_value(async(context, [] { return 42; }), [&](int& x) {
        printf("addressof x = %p, val = %i\n", (void*)&x, x);
        return async(context, [&]() -> int {
          printf("successor tranform\n");
          printf("addressof x = %p, val = %i\n", (void*)&x, x);
          return x;
        });
      }));

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 42);
  std::cout << "let_value done " << *result << "\n";
}

TEST(Let, Nested) {
  timed_single_thread_context context;
  // More complicated 'let_value' example that shows recursive let_value-scopes,
  // additional

  sync_wait(then(
      when_all(
          let_value(asyncVector(context),
              [&](std::vector<int>& v) {
                return async(context, [&] {
                  std::cout << "printing vector" << std::endl;
                  for (int& x : v) {
                    std::cout << x << ", ";
                  }
                  std::cout << std::endl;
                });
              }),
          let_value(just(42),
              [&](int& x) {
                return let_value(async(context, [&] { return x / 2; }), [&](int& y) {
                  return async(context, [&] { return x + y; });
                });
              })),
      [](std::variant<std::tuple<>> a, std::variant<std::tuple<int>> b) {
        std::cout << "when_all finished - [" << a.index() << ", "
                  << std::get<0>(std::get<0>(b)) << "]\n";
        EXPECT_EQ(a.index(), 0);
        EXPECT_EQ(b.index(), 0);
        EXPECT_EQ(std::get<0>(std::get<0>(b)), 63);
      }));
}

TEST(Let, Pipeable) {
  timed_single_thread_context context;

  // Simple usage of 'let_value()'
  // - defines an async scope in which the result of one async
  //   operation is in-scope for the duration of a second operation.
  std::optional<int> result = async(context, [] { return 42; })
    | let_value(
        [&](int& x) {
          printf("addressof x = %p, val = %i\n", (void*)&x, x);
          return async(context, [&]() -> int {
            printf("successor tranform\n");
            printf("addressof x = %p, val = %i\n", (void*)&x, x);
            return x;
          });
        })
    | sync_wait();

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 42);
  std::cout << "let_value done " << *result << "\n";
}

TEST(Let, InlineBlockingKind) {
  auto snd = let_value(just(), just);
  using Snd = decltype(snd);
  static_assert(blocking_kind::always_inline == cblocking<Snd>());
}

TEST(Let, PipeInlineBlockingKind) {
  auto snd = just() | let_value(just);
  using Snd = decltype(snd);
  static_assert(blocking_kind::always_inline == cblocking<Snd>());
}

TEST(Let, MaybeBlockingKind) {
  timed_single_thread_context context;

  auto snd1 = let_value(schedule(context.get_scheduler()), just);
  using Snd1 = decltype(snd1);
  static_assert(blocking_kind::maybe == cblocking<Snd1>());

  auto snd2 = let_value(multi_sender(), just);
  using Snd2 = decltype(snd2);
  static_assert(blocking_kind::maybe == cblocking<Snd2>());
}

TEST(Let, PipeMaybeBlockingKind) {
  timed_single_thread_context context;

  auto snd1 = just() | let_value([&] {
    return schedule(context.get_scheduler());
  });
  using Snd1 = decltype(snd1);
  static_assert(blocking_kind::maybe == cblocking<Snd1>());

  auto snd2 = just() | let_value(multi_sender);
  using Snd2 = decltype(snd2);
  static_assert(blocking_kind::maybe == cblocking<Snd2>());
}

TEST(Let, NeverBlockingKind) {
  auto snd1 = let_value(never_block(), never_block);
  using Snd1 = decltype(snd1);
  static_assert(blocking_kind::never == cblocking<Snd1>());

  timed_single_thread_context context;

  auto snd2 = let_value(schedule(context.get_scheduler()), never_block);
  using Snd2 = decltype(snd2);
  static_assert(blocking_kind::never == cblocking<Snd2>());

  auto snd3 = let_value(never_block(), multi_sender);
  using Snd3 = decltype(snd3);
  static_assert(blocking_kind::never == cblocking<Snd3>());
}

TEST(Let, PipeNeverBlockingKind) {
  auto snd1 = never_block() | let_value(never_block);
  using Snd1 = decltype(snd1);
  static_assert(blocking_kind::never == cblocking<Snd1>());

  auto snd2 = never_block() | let_value(multi_sender);
  using Snd2 = decltype(snd2);
  static_assert(blocking_kind::never == cblocking<Snd2>());
}

TEST(Let, SimpleLetValueWithAllocate) {
  std::optional<int> result =
      sync_wait(let_value(unifex::just(42), [](int num) {
        return unifex::allocate(unifex::just(num));
    }));

  EXPECT_TRUE(!!result);
  EXPECT_EQ(*result, 42);
  std::cout << "let_value with allocate done " << *result << "\n";
}

TEST(Let, SimpleLetValueVoidWithAllocate) {
  EXPECT_NO_THROW(sync_wait(let_value(unifex::just(42), [](int) {
    return unifex::allocate(unifex::just_done());
  })));
}

TEST(Let, SimpleLetValueErrorWithAllocate) {
  EXPECT_THROW(sync_wait(let_value(unifex::just(1), [](int) {
    return unifex::allocate(unifex::just_error(std::invalid_argument("Throwing error for testing purposes")));
  })), std::invalid_argument);
}

namespace {
struct TraitslessSender {
  template <typename Receiver>
  auto connect(Receiver&& receiver) {
    return unifex::connect(unifex::just(42), (Receiver &&) receiver);
  }
};
}  // namespace

namespace unifex {
template <>
struct sender_traits<TraitslessSender> : sender_traits<decltype(just(42))> {};
}  // namespace unifex

TEST(Let, LetValueWithTraitlessPredecessor) {
  auto ret = sync_wait(
      let_value(TraitslessSender{}, [](int val) { return just(val); }));
  ASSERT_TRUE(ret);
  EXPECT_EQ(*ret, 42);
}
