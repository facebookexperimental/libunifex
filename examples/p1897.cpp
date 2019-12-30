/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unifex/just.hpp>
#include <unifex/let.hpp>
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
inline constexpr sequenced_policy seq{};
}

namespace ranges {
struct int_iterator {
  const int operator[](size_t offset) const {
    return base_+offset;
  }

  const int operator*() const {
    return base_;
  }

  int_iterator& operator++() {
    ++base_;
    return *this;
  }

  int_iterator& operator++(int) {
    ++base_;
    return *this;
  }

  int base_;
};

struct iota_view {
  int size_;
};

template<class T>
int_iterator begin(const iota_view& r) {
  return int_iterator{0};
}

template<class T>
int_iterator end(const iota_view& r) {
  return int_iterator{r.size_};
}
}

int main() {
// TODO: This is still acting as transform
// TODO: This does not use the range yet or iterate
  auto result = sync_wait(indexed_for(
      just(42),
      ranges::iota_view{3},
      execution::seq,
      [](const int& x) {
        return x+1;
      }));

  std::cout << "all done " << *result << "\n";

  return 0;
}
