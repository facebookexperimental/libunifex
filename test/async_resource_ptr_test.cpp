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

#include <unifex/coroutine.hpp>

#include <unifex/async_resource_ptr.hpp>

#include <functional>
#include <gtest/gtest.h>

#include <unifex/async_manual_reset_event.hpp>

using namespace unifex;

namespace {
struct Dummy {};

TEST(AsyncResourcePtrTest, equality) {
  Dummy r;
  async_manual_reset_event evt;
  // default
  async_resource_ptr<Dummy> default1;
  async_resource_ptr<Dummy> default2;
  ASSERT_EQ(default1, default2);
  // nullptr_t
  async_resource_ptr<Dummy> null1{nullptr};
  async_resource_ptr<Dummy> null2{nullptr};
  ASSERT_EQ(null1, null2);
  // dummy
  async_resource_ptr<Dummy> dummy1{&r, &evt};
  async_resource_ptr<Dummy> dummy2{&r, &evt};
  ASSERT_EQ(dummy1, dummy2);

  ASSERT_EQ(null1, default1);
}

TEST(AsyncResourcePtrTest, unequality) {
  Dummy r1;
  Dummy r2;
  async_manual_reset_event evt1;
  async_manual_reset_event evt2;
  // dummy
  async_resource_ptr<Dummy> dummy1{&r1, &evt1};
  async_resource_ptr<Dummy> dummy2{&r2, &evt1};
  ASSERT_NE(dummy1, dummy2);

  async_resource_ptr<Dummy> dummy3{&r1, &evt1};
  async_resource_ptr<Dummy> dummy4{&r1, &evt2};
  ASSERT_NE(dummy3, dummy4);
}

TEST(AsyncResourcePtrTest, swap) {
  Dummy r;
  async_manual_reset_event evt;
  // dummy
  const async_resource_ptr<Dummy> dummy{&r, &evt};
  async_resource_ptr<Dummy> dummy1{&r, &evt};
  async_resource_ptr<Dummy> dummy2;
  ASSERT_EQ(dummy, dummy1);
  ASSERT_NE(dummy1, dummy2);

  // swap
  std::swap(dummy1, dummy2);
  ASSERT_EQ(dummy, dummy2);
  ASSERT_NE(dummy, dummy1);
}

TEST(AsyncResourcePtrTest, hash) {
  using dummy_hash = std::hash<async_resource_ptr<Dummy>>;
  Dummy r1;
  Dummy r2;
  async_manual_reset_event evt1;
  async_manual_reset_event evt2;
  // hash equal
  async_resource_ptr<Dummy> dummy1{&r1, &evt1};
  async_resource_ptr<Dummy> dummy2{&r1, &evt2};
  ASSERT_NE(dummy1, dummy2);
  ASSERT_EQ(dummy_hash{}(dummy1), dummy_hash{}(dummy2));

  // hash unequal
  async_resource_ptr<Dummy> dummy3{&r1, &evt1};
  async_resource_ptr<Dummy> dummy4{&r2, &evt1};
  ASSERT_NE(dummy3, dummy4);
  ASSERT_NE(dummy_hash{}(dummy3), dummy_hash{}(dummy4));
}
}  // namespace
