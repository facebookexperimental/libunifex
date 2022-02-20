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
#include <unifex/scheduler_concepts.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/submit.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/via.hpp>
#include <unifex/when_all.hpp>
#include <unifex/with_allocator.hpp>
#include <unifex/memory_resource.hpp>

#include <memory>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
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
    return via(scheduler, just(x))
      | then([&](int x) {
          std::printf("got %i\n", x);
          value += x;
        });
  };

  when_all(addToValue(1), addToValue(2))
    | with_allocator(allocator)
    | sync_wait();

  EXPECT_EQ(value, 3);
}
} // anonymous namespace

TEST(with_allocator, SubmitWithStdAllocator) {
  single_thread_context thread;
  test(thread.get_scheduler(), std::allocator<std::byte>{});
}

#if !UNIFEX_NO_MEMORY_RESOURCE
TEST(with_allocator, SubmitWithCountingAllocator) {
  counting_memory_resource res{new_delete_resource()};
  polymorphic_allocator<char> alloc{&res};

  {
    single_thread_context thread;
    test(thread.get_scheduler(), alloc);
  }

  // Check that it freed all the memory it allocated
  EXPECT_EQ(res.total_allocated_bytes(), 0);

  // Check that it actually called allocate()
  EXPECT_EQ(res.total_allocation_count(), 2);
}

#endif
