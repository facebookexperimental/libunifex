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
#include <iostream>
#include <memory>

#include <unifex/vtable.hpp>

#include <gtest/gtest.h>

struct simple_vtable_t {
  UNIFEX_VTABLE_DECLARE;

  UNIFEX_VTABLE_ENTRY_VOID(foo, int);
  UNIFEX_VTABLE_ENTRY(bar, int, double, double);
};

struct class_one {
  int do_foo() noexcept { return 1; }
  int do_bar(double, double) { return 0; }

  simple_vtable_t table_ =
      UNIFEX_VTABLE_CONSTRUCT(&class_one::do_foo, &class_one::do_bar);
};

struct class_two {
  explicit class_two(int v) : value_(v){};
  class_two() = default;

  int value_ = 2;

  int foo_fn() noexcept { return value_; }
  int bar_fn(double, double) { return 1; }

  simple_vtable_t table_ =
      UNIFEX_VTABLE_CONSTRUCT(&class_two::foo_fn, &class_two::bar_fn);
};

TEST(vtable, bar_returns_expected) {
  auto instance_0 = std::make_unique<class_one>();
  ASSERT_TRUE(!!instance_0->table_);
  EXPECT_EQ(0, instance_0->table_.bar(0, 0));

  auto instance_1 = std::make_unique<class_two>();
  ASSERT_TRUE(!!instance_1->table_);
  EXPECT_EQ(1, instance_1->table_.bar(0, 0));
}

TEST(vtable, foo_indirect_goes_to_instance) {
  auto instance_0 = std::make_unique<class_one>();
  auto instance_1 = std::make_unique<class_two>();
  auto instance_2 = std::make_unique<class_two>(3);

  simple_vtable_t* tables[] = {
      &instance_0->table_, &instance_1->table_, &instance_2->table_};

  for (auto& i : tables) {
    ASSERT_TRUE(!!i);
  }

  EXPECT_EQ(tables[0]->foo(), 1);
  EXPECT_EQ(tables[1]->foo(), 2);
  EXPECT_EQ(tables[2]->foo(), 3);
}

struct reference_vtable_t {
  UNIFEX_VTABLE_DECLARE;
  UNIFEX_VTABLE_ENTRY(foo, std::string, std::string&&);
  UNIFEX_VTABLE_ENTRY(bar, std::string, std::string&);
};

struct class_three {
  std::string foo(std::string&& s) { return std::move(s); };
  std::string bar(std::string& s) {
    auto r = s;
    s = "";
    return r;
  };

  reference_vtable_t table_ =
      UNIFEX_VTABLE_CONSTRUCT(&class_three::foo, &class_three::bar);
};

TEST(vtable, perfect_forwarding) {
  auto instance = std::make_unique<class_three>();
  ASSERT_TRUE(!!instance->table_);

  static constexpr auto a = "string a";
  std::string ao = a;
  auto ar = instance->table_.foo(std::move(ao));
  EXPECT_EQ(a, ar);

  static constexpr auto b = "string b";
  std::string bo = b;
  auto br = instance->table_.bar(bo);
  EXPECT_EQ(b, br);
  EXPECT_EQ("", bo);
}

struct class_four {
  struct vtable {
    UNIFEX_VTABLE_DECLARE;
    UNIFEX_VTABLE_ENTRY_VOID_RVALUE(foo, void);
    UNIFEX_VTABLE_ENTRY_RVALUE(bar, void, int);
  };

  void foo() && {};
  void bar(int) && {};

  vtable table_ = UNIFEX_VTABLE_CONSTRUCT(&class_four::foo, &class_four::bar);
};

TEST(vtable, ref_qualifier) {
  auto instance = std::make_unique<class_four>();
  std::move(instance->table_).foo();
  std::move(instance->table_).bar(1);
}
