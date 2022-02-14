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
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/indexed_for.hpp>
#include <unifex/when_all.hpp>

#include <chrono>
#include <iostream>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace execution {
class sequenced_policy {};
class parallel_policy{};
inline constexpr sequenced_policy seq{};
inline constexpr parallel_policy par{};
}

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

int main() {
  // use seq, which supports a forward range
  auto result = sync_wait(indexed_for(
      just(42),
      execution::seq,
      ranges::iota_view{10},
      [](int idx, int& x) {
        x = x + idx;
      }));

  std::cout << "all done " << *result << "\n";

  // indexed_for example from P1897R2:
  auto  just_sender =
    just(std::vector<int>{3, 4, 5}, 10);

  // Use par which requires range to be random access
  auto indexed_for_sender =
    indexed_for(
      std::move(just_sender),
      execution::par,
      ranges::iota_view{3},
      [](int idx, std::vector<int>& vec, const int& i){
        vec[idx] = vec[idx] + i + idx;
      });

  auto transform_sender = then(
    std::move(indexed_for_sender), [](std::vector<int> vec, int /*i*/){return vec;});

  // Slight difference from p1897R2 because unifex's sync_wait returns an optional
  // to account for cancellation
  std::vector<int> vector_result =
    *sync_wait(std::move(transform_sender));

  std::cout << "vector result:\n";
  for(auto v : vector_result) {
    std::cout << "\t" << v << "\n";
  }

  return 0;
}
