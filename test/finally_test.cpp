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
#include <unifex/finally.hpp>

#include <unifex/inplace_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/just_from.hpp>
#include <unifex/let_done.hpp>
#include <unifex/let_error.hpp>
#include <unifex/let_value.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/upon_error.hpp>

#include <cstdio>
#include <thread>
#include <tuple>
#include <variant>

#include <gtest/gtest.h>

using namespace unifex;

TEST(Finally, Value) {
  timed_single_thread_context context;

  auto res = just(42)
    | finally(schedule(context.get_scheduler()))
    | then([](int i){ return std::make_pair(i, std::this_thread::get_id() ); })
    | sync_wait();

  ASSERT_FALSE(!res);
  EXPECT_EQ(res->first, 42);
  EXPECT_EQ(res->second, context.get_thread_id());
}

TEST(Finally, Ref) {
  {
    int a = 0;

    auto sndr = just_from([&a]() -> int& { return a; })
      | finally(just());
    using Sndr = decltype(sndr);

    static_assert(std::is_same_v<
     sender_value_types_t<Sndr, std::variant, std::tuple>,
      std::variant<std::tuple<int&>>
    >);
    static_assert(std::is_same_v<
      sender_error_types_t<Sndr, std::variant>,
      std::variant<std::exception_ptr>
    >);
    static_assert(!sender_traits<Sndr>::sends_done);

    auto res = std::move(sndr) | sync_wait();

    ASSERT_FALSE(!res);
    EXPECT_EQ(&res->get(), &a);
  }

  {
    int a = 0;

    auto res = just_from([&a]() -> const int& { return a; })
      | finally(just())
      | sync_wait();

    ASSERT_FALSE(!res);
    EXPECT_EQ(&res->get(), &a);
  }

  {
    int a = 0;

    auto res = just_from([&a]() -> int& { return a; })
      | finally(just())
      | then([](int& i) -> int& { return i; })
      | sync_wait();

    ASSERT_FALSE(!res);
    EXPECT_EQ(&res->get(), &a);
  }
}

struct sends_error_ref {

  template <
    template <typename...> class Variant,
    template <typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = Variant<int>;

  static constexpr bool sends_done = false;

  template <class Receiver>
  struct operation {
    friend auto tag_invoke(tag_t<start>, operation& self) noexcept {
      set_error(std::move(self.receiver), self.val);
    }

    int& val;
    Receiver receiver;
  };

  template <class Receiver>
  friend auto tag_invoke(tag_t<connect>, sends_error_ref self, Receiver&& receiver) {
    return operation<Receiver>{self.val, std::forward<Receiver>(receiver)};
  }

  int& val;
};


TEST(Finally, ErrorRefDecays) {
  // TODO: Should errors also have references preserved in 'finally?' See the
  // 'finally' discussion in issue #541.
  int a = 0;
  auto sndr = sends_error_ref{a} | finally(just());
  using Sndr = decltype(sndr);

  static_assert(std::is_same_v<
    sender_value_types_t<Sndr, std::variant, std::tuple>,
    std::variant<std::tuple<>>
  >);
  static_assert(std::is_same_v<
    sender_error_types_t<Sndr, std::variant>,
    std::variant<int, std::exception_ptr>
  >);
  static_assert(!sender_traits<Sndr>::sends_done);
}

TEST(Finally, Done) {
  timed_single_thread_context context;

  auto res = just_done()
    | finally(schedule(context.get_scheduler()))
    | let_done([](){ return just(std::this_thread::get_id()); })
    | sync_wait();

  ASSERT_FALSE(!res);
  EXPECT_EQ(*res, context.get_thread_id());
}

TEST(Finally, Error) {
  timed_single_thread_context context;

  auto res = just_error(-1)
    | finally(schedule(context.get_scheduler()))
    | let_error([](auto&&){ return just(std::this_thread::get_id()); })
    | sync_wait();

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, context.get_thread_id());
}

TEST(Finally, BlockingKind) {
  auto snd1 = finally(just(), just());
  using Snd1 = decltype(snd1);
  static_assert(blocking_kind::always_inline == sender_traits<Snd1>::blocking);

  timed_single_thread_context context;

  auto snd2 = finally(just(), schedule(context.get_scheduler()));
  using Snd2 = decltype(snd2);
  static_assert(blocking_kind::never == sender_traits<Snd2>::blocking);
}

TEST(Finally, CombinedWithLetValue) {
  const int i{42};
  auto ret = let_value(
                 just(&i),
                 [](const int* pi) noexcept {
                   return just(pi) |
                       then([](const int* pi) -> const int& { return *pi; });
                 }) |
      finally(just()) | then([](const int& i) { return &i; }) | sync_wait();

  ASSERT_TRUE(ret.has_value());
  EXPECT_EQ(&i, *ret);
}
