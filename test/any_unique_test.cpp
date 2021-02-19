/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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
#include <unifex/any_unique.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/memory_resource.hpp>
#include <unifex/type_index.hpp>

#include <string>
#include <atomic>
#include <sstream>

#include <gtest/gtest.h>

namespace {
inline constexpr struct get_typeid_cpo {
  using type_erased_signature_t =
      unifex::type_index(const unifex::this_&) noexcept;

  template <typename T>
  friend unifex::type_index tag_invoke(get_typeid_cpo, const T& x) {
    return unifex::type_id<T>();
  }

  template <typename T>
  auto operator()(const T& x) const noexcept ->
      unifex::tag_invoke_result_t<get_typeid_cpo, const T&> {
    static_assert(
      unifex::is_same_v<unifex::type_index, unifex::tag_invoke_result_t<get_typeid_cpo, const T&>>);
    return tag_invoke(get_typeid_cpo{}, x);
  }
} get_typeid{};

inline constexpr struct to_string_cpo {
  using type_erased_signature_t =
      std::string(const unifex::this_&) noexcept;

  template <typename T>
  friend std::string tag_invoke(to_string_cpo, const T& x) {
    std::stringstream sout;
    sout << x;
    return sout.str();
  }

  template <typename T>
  auto operator()(const T& x) const noexcept ->
      unifex::tag_invoke_result_t<to_string_cpo, const T&> {
    static_assert(
      unifex::is_same_v<std::string, unifex::tag_invoke_result_t<to_string_cpo, const T&>>);
    return tag_invoke(to_string_cpo{}, x);
  }
} to_string{};

struct destructor {
  explicit destructor(bool& x) : ref_(x) {}
  ~destructor() {
    ref_ = true;
  }
  bool& ref_;
};

#if !UNIFEX_NO_MEMORY_RESOURCE
using namespace unifex::pmr;

class counting_memory_resource : public memory_resource {
 public:
  explicit counting_memory_resource(memory_resource* r) noexcept : inner_(r) {}

  std::size_t total_allocated_bytes() const {
    return allocated_.load();
  }

 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    void* p = inner_->allocate(bytes, alignment);
    allocated_ += bytes;
    return p;
  }

  void do_deallocate(void* p, std::size_t bytes, std::size_t alignment)
      override {
    allocated_ -= bytes;
    inner_->deallocate(p, bytes, alignment);
  }

  bool do_is_equal(const memory_resource& other) const noexcept override {
    return &other == this;
  }

  memory_resource* inner_;
  std::atomic<std::size_t> allocated_ = 0;
};
#endif
} // anonymous namespace

using A = unifex::any_unique_t<get_typeid>;
using B = unifex::any_unique_t<>;

static_assert(unifex::movable<A>);

TEST(AnyUniqueTest, WithTypeid) {
  const ::A a = std::string{"hello"};
  auto id = get_typeid(a);
  EXPECT_EQ(id, unifex::type_id<std::string>());
}

TEST(AnyUniqueTest, WithoutTypeid) {
  const ::B b = std::string{"hello"};
  auto id = get_typeid(b);
  EXPECT_EQ(id, unifex::type_id<B>());
}

TEST(AnyUniqueTest, TestDestructor) {
  bool hasDestructorRun = false;
  {
    const A a{unifex::in_place_type_t<destructor>{}, hasDestructorRun};
    EXPECT_EQ(get_typeid(a), unifex::type_id<destructor>());
    EXPECT_FALSE(hasDestructorRun);
  }
  EXPECT_TRUE(hasDestructorRun);
}

using Aref = unifex::any_ref_t<get_typeid, to_string>;
using Bref = unifex::any_ref_t<>;

TEST(AnyRefTest, WithTypeid) {
  std::string hello{"hello"};
  const ::Aref a = hello;
  auto id = get_typeid(a);
  EXPECT_EQ(id, unifex::type_id<std::string>());
  std::string str = to_string(a);
  EXPECT_EQ(str, "hello");
}

TEST(AnyRefTest, WithoutTypeid) {
  std::string hello{"hello"};
  const ::Bref b = hello;
  auto id = get_typeid(b);
  EXPECT_EQ(id, unifex::type_id<Bref>());
}

#if !UNIFEX_NO_MEMORY_RESOURCE
TEST(AnyUniqueTest, WithCustomAllocator) {
  counting_memory_resource res{new_delete_resource()};
  polymorphic_allocator<char> alloc{&res};
  {
    A a1{std::string("hello"), alloc};
    EXPECT_GE(res.total_allocated_bytes(), sizeof(std::string));
    A a2{std::allocator_arg, alloc, unifex::in_place_type_t<std::string>{}, "hello"};
    EXPECT_GE(res.total_allocated_bytes(), 2 * sizeof(std::string));
  }
  EXPECT_EQ(res.total_allocated_bytes(), 0);
}
#endif
