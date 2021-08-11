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
#include <unifex/then.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/indexed_for.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;

namespace execution {
class sequenced_policy {};
class parallel_policy{};
inline constexpr sequenced_policy seq{};
inline constexpr parallel_policy par{};
}

namespace {
namespace ranges {
struct int_iterator {
  using value_type = int;
  using reference = value_type&;
  using difference_type = size_t;
  using pointer = value_type*;
  using iterator_category = std::random_access_iterator_tag;

  int operator[](size_t offset) const {
    return base_ + static_cast<int>(offset);
  }

  int operator*() const {
    return base_;
  }

  int_iterator operator++() {
    ++base_;
    return *this;
  }

  int_iterator operator++(int) {
    auto cur = *this;
    ++base_;
    return cur;
  }

  bool operator!=(const int_iterator& rhs) const {
    return base_ != rhs.base_;
  }

  int base_;
};

struct iota_view {
  int size_;
  using iterator = int_iterator;

  int_iterator begin() {
    return int_iterator{0};
  }

  int_iterator end() {
    return int_iterator{size_};
  }

  size_t size() const {
    return size_;
  }
};
} // namespace ranges
} // anonymous namespace

TEST(indexed_for, Pipeable) {
  // use seq, which supports a forward range
  auto result = just(42)
    | indexed_for(
        execution::seq,
        ranges::iota_view{10},
        [](int idx, int& x) {
          x = x + idx;
        })
    | sync_wait();

  // ranges::iota_view{10} produces [0, 9] so our accumulator is summing
  // 42 + 0 + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 +  9, which is 42 + 45 = 87.
  EXPECT_EQ(87, *result);
}
