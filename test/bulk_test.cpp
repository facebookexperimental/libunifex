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
#include <unifex/into_variant.hpp>
#include <unifex/when_all.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/bulk.hpp>
#include <unifex/just.hpp>
#include <unifex/sync_wait.hpp>
#include <type_traits>
#include <variant>
#include <tuple>
#include <vector>
#include <gtest/gtest.h>

using namespace unifex;

template <typename... T>
using vt_t = std::variant<std::tuple<T...>>;

using std::is_same_v;
using std::variant;
using std::tuple;

TEST(Bulk, StaticTypeCheck){
  auto snd1 = just(42)
            | bulk(3, [](int, int){});
  static_assert(is_same_v<decltype(snd1)::value_types<variant, tuple>,
      vt_t<int>>);
  static_assert(is_same_v<decltype(snd1)::error_types<variant>,
      variant<std::exception_ptr>>);
  static_assert(snd1.sends_done == false);

  auto snd2 = just_error(42)
            | bulk(3, [](int){});
  static_assert(is_same_v<decltype(snd2)::value_types<variant, tuple>,
      variant<>>);
  static_assert(is_same_v<decltype(snd2)::error_types<variant>,
      variant<int, std::exception_ptr>>);
  static_assert(snd2.sends_done == false);

  auto snd3 = just_done()
            | bulk(3, [](int){});
  static_assert(is_same_v<decltype(snd3)::value_types<variant, tuple>,
      variant<>>);
  static_assert(is_same_v<decltype(snd3)::error_types<variant>,
      variant<std::exception_ptr>>);
  static_assert(snd3.sends_done == true);

  auto snd4 = when_all(just(42), just("string"))
           | bulk(3, [&](auto, auto, auto){
             });
  static_assert(is_same_v<decltype(snd4)::value_types<variant, tuple>,
      vt_t<vt_t<int>, vt_t<const char*>>>);
  static_assert(is_same_v<decltype(snd4)::error_types<variant>,
      variant<std::exception_ptr>>);
  static_assert(snd4.sends_done == true);
}

TEST(Bulk, Working){
  const int size = 3;
  std::vector<int> check_vec(size);
  const std::vector<int> expected_vec{42, 43, 44};
  auto val = sync_wait(bulk(just(42), size, [&](int idx, int val){
               check_vec[idx] = val + idx;
             }));
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 42);
  EXPECT_EQ(check_vec, expected_vec);
}

TEST(Bulk, Pipeable){
  const int size = 3;
  std::vector<int> check_vec(size);
  const std::vector<int> expected_vec{42, 43, 44};
  auto val = just(42)
           | bulk(size, [&](int idx, int val){
               check_vec[idx] = val + idx;
             })
           | sync_wait();
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 42);
  EXPECT_EQ(check_vec, expected_vec);
}

TEST(Bulk, WithMultipleReturnValue){
  const int size = 3;
  std::vector<std::string> check_vec;
  const std::vector<std::string> expected_vec{"string42", "string43", "string44"};
  auto val = when_all(just(42), just("string"))
           | bulk(size, [&](int idx, vt_t<int> val_cont, vt_t<const char*> str_cont){
               auto [val] = std::get<0>(val_cont);
               auto [str] = std::get<0>(str_cont);
               check_vec.emplace_back(str + std::to_string(val + idx));
             })
           | into_variant()
           | sync_wait();
  EXPECT_TRUE(val.has_value());
  const auto& [value_cont, str_cont] = std::get<0>(val.value());
  const auto [value] = std::get<0>(value_cont);
  const auto [str] = std::get<0>(str_cont);
  EXPECT_EQ(value, 42);
  EXPECT_EQ(str, std::string("string"));
  EXPECT_EQ(check_vec, expected_vec);
}

TEST(Bulk, WithNoReturnValue){
  const int size = 3;
  std::vector<int> check_vec;
  const std::vector<int> expected_vec{0, 1, 2};
  just()
    | bulk(size, [&](int idx){
        check_vec.push_back(idx);
      })
    | sync_wait();
  EXPECT_EQ(check_vec, expected_vec);
}
