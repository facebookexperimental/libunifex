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
#include <unifex/scheduler_concepts.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/submit.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/via.hpp>
#include <unifex/when_all.hpp>
#include <unifex/with_allocator.hpp>
#include <unifex/memory_resource.hpp>

#include <memory>

using namespace unifex;

#if !UNIFEX_NO_MEMORY_RESOURCE
using namespace unifex::pmr;

class counting_memory_resource : public memory_resource {
public:
  explicit counting_memory_resource(memory_resource *r) noexcept : inner_(r) {}

  std::size_t total_allocated_bytes() const { return allocated_.load(); }

  std::size_t total_allocation_count() const { return count_.load(); }

private:
  void *do_allocate(std::size_t bytes, std::size_t alignment) override {
    void *p = inner_->allocate(bytes, alignment);
    allocated_ += bytes;
    ++count_;
    return p;
  }

  void do_deallocate(void *p, std::size_t bytes,
                     std::size_t alignment) override {
    allocated_ -= bytes;
    inner_->deallocate(p, bytes, alignment);
  }

  bool do_is_equal(const memory_resource &other) const noexcept override {
    return &other == this;
  }

  memory_resource *inner_;
  std::atomic<std::size_t> allocated_ = 0;
  std::atomic<std::size_t> count_ = 0;
};
#endif

template <typename Scheduler, typename Allocator>
void test(Scheduler scheduler, Allocator allocator) {
  int value = 0;

  auto addToValue = [&](int x) {
    // The via() is expected to allocate when it calls submit().
    // NOTE: This may start failing if we ever merge via() and typed_via().
    return transform(via(schedule(scheduler), just(x)), [&](int x) {
      std::printf("got %i\n", x);
      value += x;
    });
  };

  sync_wait(with_allocator(when_all(addToValue(1), addToValue(2)), allocator));

  assert(value == 3);
}

int main() {
  single_thread_context thread;

  test(thread.get_scheduler(), std::allocator<std::byte>{});

#if !UNIFEX_NO_MEMORY_RESOURCE
  {
    counting_memory_resource res{new_delete_resource()};
    polymorphic_allocator<char> alloc{&res};
    test(thread.get_scheduler(), alloc);

    // Check that it freed all the memory it allocated
    if (res.total_allocated_bytes() != 0) {
      std::printf("error: didn't free all memory!\n");
      return -1;
    }

    // Check that it actually called allocate()
    if (res.total_allocation_count() != 2) {
      std::printf("error: didn't perform expected number of allocations\n");
      return -1;
    }
  }
#endif
}
