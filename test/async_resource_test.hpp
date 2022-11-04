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

#include <unifex/async_resource.hpp>

#include <functional>
#include <gtest/gtest.h>

#include <unifex/defer.hpp>
#include <unifex/just.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/spawn_detached.hpp>
#include <unifex/then.hpp>
#include <unifex/v2/async_scope.hpp>

namespace unifex_test {

struct AsyncResourceTest : testing::Test {
  unifex::v2::async_scope outerScope;
  unifex::single_thread_context ctx;

  ~AsyncResourceTest() override { EXPECT_EQ(objectCount, 0); }

  std::atomic_int objectCount{0};
};

template <typename T>
struct AsyncResourceTypedTest : public AsyncResourceTest {};

class ResourceBase {
  std::atomic_int* objectCount_;

public:
  explicit ResourceBase(std::atomic_int* objectCount) noexcept
    : objectCount_(objectCount) {
    ++(*objectCount_);
  }

  ~ResourceBase() { --(*objectCount_); }
};

struct UnmanagedResource : public ResourceBase {
  using ResourceBase::ResourceBase;

  // suppress deprecation warning - noop
  auto destroy() noexcept { return unifex::just(); }
};

struct TwinNestingResource {
  TwinNestingResource() = default;

  TwinNestingResource(
      unifex::async_resource_ptr<UnmanagedResource> child1,
      unifex::async_resource_ptr<UnmanagedResource> child2) noexcept
    : child1_(std::move(child1))
    , child2_(std::move(child2)) {}

  void swap1(unifex::async_resource_ptr<UnmanagedResource> child) noexcept {
    child1_.swap(child);
  }

  void swap2(unifex::async_resource_ptr<UnmanagedResource> child) noexcept {
    child2_.swap(child);
  }

  void drop_children() noexcept {
    std::move(child1_).reset();
    std::move(child2_).reset();
  }
  //
  // suppress deprecation warning - noop
  auto destroy() noexcept { return unifex::just(); }

private:
  unifex::async_resource_ptr<UnmanagedResource> child1_;
  unifex::async_resource_ptr<UnmanagedResource> child2_;
};

template <typename Resource>
struct SingleNestingResource {
  SingleNestingResource(unifex::async_resource_ptr<Resource> child) noexcept
    : child_(std::move(child)) {}

  void drop_child() noexcept { std::move(child_).reset(); }
  //
  // suppress deprecation warning - noop
  auto destroy() noexcept { return unifex::just(); }

private:
  unifex::async_resource_ptr<Resource> child_;
};

struct ThrowingResource {
  ThrowingResource() { throw 42; }
  // suppress deprecation warning - noop
  auto destroy() noexcept { return unifex::just(); }
};

template <typename Scheduler>
struct ThrowingSpawningResource {
  ThrowingSpawningResource(
      unifex::async_scope_ref scope, Scheduler sched, AsyncResourceTest* f) {
    unifex::spawn_detached(
        unifex::on(
            sched,
            unifex::defer([sched, scope, f]() noexcept {
              return unifex::make_async_resource(
                  sched, scope, [f](auto, auto) noexcept {
                    return UnmanagedResource{&(f->objectCount)};
                  });
            }) | unifex::then([](auto&&) noexcept { /* drop */ })),
        scope);

    throw 42;
  }
  // suppress deprecation warning - noop
  auto destroy() noexcept { return unifex::just(); }
};
}  // namespace unifex_test
