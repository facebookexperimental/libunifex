/*
 * Copyright (c) Rishabh Dwivedi <rishabhdwivedi17@gmail.com>
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
#include <unifex/let_done.hpp>
#include <unifex/then.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/when_all.hpp>
#include <unifex/just_error.hpp>
#include <unifex/just.hpp>
#include <unifex/into_variant.hpp>
#include <unifex/just_done.hpp>
#include <exception>
#include <variant>
#include <gtest/gtest.h>

using namespace unifex;

template <typename... T>
using vt_t = std::variant<std::tuple<T...>>;

using std::variant;
using std::tuple;
using std::is_same_v;


TEST(IntoVariant, StaticTypeCheck){
  auto snd1 = just(42)
            | into_variant();
  static_assert(is_same_v<decltype(snd1)::value_types<variant, tuple>,
      vt_t<vt_t<int>>>);
  static_assert(is_same_v<decltype(snd1)::error_types<variant>,
      variant<std::exception_ptr>>);
  static_assert(decltype(snd1)::sends_done == false);

  auto snd2 = just_error(42)
            | into_variant();
  static_assert(is_same_v<decltype(snd2)::value_types<variant, tuple>,
      vt_t<variant<>>>);
  static_assert(is_same_v<decltype(snd2)::error_types<variant>,
      variant<int>>);
  static_assert(decltype(snd2)::sends_done == false);

  auto snd3 = just_done()
            | into_variant();
  static_assert(is_same_v<decltype(snd3)::value_types<variant, tuple>,
      vt_t<variant<>>>);
  static_assert(is_same_v<decltype(snd3)::error_types<variant>,
      variant<>>);
  static_assert(decltype(snd3)::sends_done == true);

  auto snd4 = when_all( just(42), just(42.0), just_error(42) )
            | into_variant();
  static_assert(is_same_v<decltype(snd4)::value_types<variant, tuple>,
      vt_t<vt_t<vt_t<int>, vt_t<double>, variant<>>>>);
  static_assert(is_same_v<decltype(snd4)::error_types<variant>,
      variant<std::exception_ptr, int>>);
  static_assert(decltype(snd4)::sends_done == true);

  auto snd5 = just(42) | let_done([]{return just("hello");}) | into_variant();
  static_assert(is_same_v<decltype(snd5)::value_types<variant, tuple>,
      vt_t<variant<tuple<int>, tuple<const char*>>>>);
  static_assert(is_same_v<decltype(snd5)::error_types<variant>,
      variant<std::exception_ptr>>);
  static_assert(decltype(snd5)::sends_done == false);
}

TEST(IntoVariant, Working){
  bool called = false;
  auto x = sync_wait(into_variant(when_all(
          just(42),
          just(42.0) | then([&](double d){
            called = true;
            return d + 1;
          }))));
  EXPECT_TRUE(called);
  EXPECT_TRUE(x.has_value());
  const auto& [vt_first_val, vt_second_val] = std::get<0>(x.value());
  const auto& [first_val] = std::get<0>(vt_first_val);
  const auto [second_val] = std::get<0>(vt_second_val);
  EXPECT_EQ(first_val, 42);
  EXPECT_EQ(second_val, 43.0);
}

TEST(IntoVariant, Pipeable){
  bool called = false;
  auto x = when_all(
      just(42),
      just(42.5)
      | then([&](double d){
          called = true;
          return d + 1;
        }))
      | into_variant()
      | sync_wait();
  EXPECT_TRUE(called);
  EXPECT_TRUE(x.has_value());
  const auto& [vt_first_val, vt_second_val] = std::get<0>(x.value());
  const auto& [first_val] = std::get<0>(vt_first_val);
  const auto [second_val] = std::get<0>(vt_second_val);
  EXPECT_EQ(first_val, 42);
  EXPECT_EQ(second_val, 43.5);
}

TEST(IntoVariant, OneOfPossibleValues){
  bool called = false;
  auto x = just(42)
    | let_done([&]{ called = true; return just(42.5); }) 
    | into_variant()
    | sync_wait();
  EXPECT_FALSE(called);
  EXPECT_TRUE(x.has_value());
  const auto& [val] = std::get<0>(x.value());
  EXPECT_EQ(val, 42);
}
