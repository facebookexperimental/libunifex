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

#include <unifex/any_object.hpp>

#include <unifex/overload.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/this.hpp>
#include <unifex/type_index.hpp>

#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include <unifex/detail/prologue.hpp>

namespace
{
  inline constexpr struct get_typeid_cpo {
    using type_erased_signature_t =
        unifex::type_index(const unifex::this_&) noexcept;

    template <typename T>
    friend unifex::type_index tag_invoke(get_typeid_cpo, const T&) {
      return unifex::type_id<T>();
    }

    template(typename T)                                            //
        (requires unifex::tag_invocable<get_typeid_cpo, const T&>)  //
        auto
        operator()(const T& x) const noexcept
        -> unifex::tag_invoke_result_t<get_typeid_cpo, const T&> {
      static_assert(std::is_same_v<
                    unifex::type_index,
                    unifex::tag_invoke_result_t<get_typeid_cpo, const T&>>);
      return unifex::tag_invoke(get_typeid_cpo{}, x);
    }
  } get_typeid{};

  inline constexpr struct to_string_cpo {
    using type_erased_signature_t = std::string(const unifex::this_&);

    template(typename T)(
        requires unifex::tag_invocable<to_string_cpo, const T&> AND
            unifex::convertible_to<
                unifex::tag_invoke_result_t<to_string_cpo, const T&>,
                std::string>) std::string
    operator()(const T& value) const {
      return unifex::tag_invoke(to_string_cpo{}, value);
    }

    template <
        typename T,
        std::enable_if_t<!unifex::tag_invocable<to_string_cpo, const T&>, int> =
            0,
        std::void_t<decltype(std::to_string(std::declval<const T&>()))>* =
            nullptr>
    std::string operator()(const T& x) const {
      return std::to_string(x);
    }
  } to_string;

  using any_typeidable = unifex::basic_any_object<
      8,
      8,
      true,
      std::allocator<std::byte>,
      unifex::tag_t<get_typeid>>;

  struct instance_counter {
  private:
    static int constructor_count;
    static int destructor_count;

  public:
    int id;
    int original_id;

    static void reset_counts() {
      constructor_count = 0;
      destructor_count = 0;
    }
    static size_t get_constructor_count() noexcept { return constructor_count; }
    static size_t get_destructor_count() noexcept { return destructor_count; }
    static size_t get_instance_count() noexcept {
      return constructor_count - destructor_count;
    }

    instance_counter() noexcept : id(constructor_count++), original_id(id) {}

    instance_counter(const instance_counter& x) noexcept
      : id(constructor_count++)
      , original_id(x.original_id) {}

    instance_counter(instance_counter&& x) noexcept
      : id(constructor_count++)
      , original_id(x.original_id) {
      x.original_id = -x.original_id;
    }

    ~instance_counter() { ++destructor_count; }

    instance_counter& operator=(const instance_counter& x) noexcept {
      original_id = x.original_id;
      return *this;
    }

    instance_counter&& operator=(instance_counter&& x) noexcept {
      original_id = x.original_id;
      x.original_id = -x.original_id;
      return std::move(*this);
    }

    friend std::string
    tag_invoke(unifex::tag_t<to_string>, const instance_counter& x) {
      return std::to_string(x.id) + " (" + std::to_string(x.original_id) + ")";
    }
  };

  int instance_counter::constructor_count = 0;
  int instance_counter::destructor_count = 0;
}  // namespace

TEST(AnyObjectTest, ImplicitConstruction) {
  any_typeidable x = 99;
  EXPECT_TRUE(get_typeid(x) == unifex::type_id<int>());

  any_typeidable y = 1.0f;
  EXPECT_TRUE(get_typeid(y) == unifex::type_id<float>());
}

TEST(AnyObjectTest, InPlaceConstruction) {
  {
    struct some_default_constructible {};
    any_typeidable a(std::in_place_type<some_default_constructible>);
    EXPECT_TRUE(get_typeid(a) == unifex::type_id<some_default_constructible>());
  }
  {
    // With conversion
    any_typeidable x(std::in_place_type<double>, 42.0f);
    EXPECT_TRUE(get_typeid(x) == unifex::type_id<double>());
  }
}

TEST(AnyObjectTest, InPlaceConstructionOnlyConstructsOnce) {
  instance_counter::reset_counts();

  {
    any_typeidable x(std::in_place_type<instance_counter>);
    EXPECT_EQ(instance_counter::get_constructor_count(), 1);
    EXPECT_EQ(instance_counter::get_instance_count(), 1);
  }

  EXPECT_EQ(instance_counter::get_instance_count(), 0);
  EXPECT_EQ(instance_counter::get_destructor_count(), 1);
}

TEST(AnyObjectTest, MoveConstructionMovesSmallObjects) {
  instance_counter::reset_counts();

  using any_small_object = unifex::basic_any_object<
      sizeof(instance_counter),
      alignof(instance_counter),
      true,
      std::allocator<std::byte>,
      unifex::tag_t<get_typeid>>;

  {
    any_small_object x(std::in_place_type<instance_counter>);

    EXPECT_EQ(instance_counter::get_instance_count(), 1);

    {
      any_small_object y{std::move(x)};

      EXPECT_EQ(instance_counter::get_instance_count(), 2);
    }

    EXPECT_EQ(instance_counter::get_instance_count(), 1);
  }

  EXPECT_EQ(instance_counter::get_instance_count(), 0);
  EXPECT_EQ(instance_counter::get_constructor_count(), 2);
}

TEST(AnyObjectTest, MoveConstructorDoesNotMoveLargeObjects) {
  instance_counter::reset_counts();

  struct big_instance_counter : instance_counter {
    std::byte extra[40];
  };

  using any_small_object = unifex::basic_any_object<
      sizeof(instance_counter),
      alignof(instance_counter),
      true,
      std::allocator<std::byte>,
      unifex::tag_t<get_typeid>>;

  {
    any_small_object x(std::in_place_type<big_instance_counter>);

    EXPECT_EQ(instance_counter::get_instance_count(), 1);

    {
      any_small_object y{std::move(x)};

      EXPECT_EQ(instance_counter::get_instance_count(), 1);
    }

    EXPECT_EQ(instance_counter::get_instance_count(), 0);
  }

  EXPECT_EQ(instance_counter::get_instance_count(), 0);
  EXPECT_EQ(instance_counter::get_constructor_count(), 1);
}

namespace
{
  template <typename T>
  struct always_fails_allocator {
    using value_type = T;

    always_fails_allocator() = default;

    template <typename OtherT>
    always_fails_allocator(always_fails_allocator<OtherT>) noexcept {}

    T* allocate(size_t) { throw std::bad_alloc{}; }

    void deallocate(T*, size_t) { std::terminate(); }
  };

  template <size_t Size, size_t Alignment>
  struct alignas(Alignment) sized_type {
    std::byte storage[Size];
  };

}  // namespace

TEST(AnyObjectTest, SmallObjectsDontCallAllocator) {
  using any_small_object = unifex::basic_any_object<
      16,
      8,
      true,
      always_fails_allocator<std::byte>,
      get_typeid_cpo>;

  any_small_object x(std::in_place_type<sized_type<4, 4>>);

  EXPECT_NO_THROW((any_small_object(std::in_place_type<sized_type<4, 4>>)));
  EXPECT_NO_THROW((any_small_object(std::in_place_type<sized_type<8, 4>>)));
  EXPECT_NO_THROW((any_small_object(std::in_place_type<sized_type<16, 4>>)));
  EXPECT_NO_THROW((any_small_object(std::in_place_type<sized_type<4, 8>>)));
  EXPECT_NO_THROW((any_small_object(std::in_place_type<sized_type<16, 8>>)));
}

TEST(AnyObjectTest, LargeObjectsCallAllocator) {
  using any_small_object = unifex::basic_any_object<
      16,
      8,
      true,
      always_fails_allocator<std::byte>,
      get_typeid_cpo>;

  EXPECT_THROW(
      (any_small_object(std::in_place_type<sized_type<32, 4>>)),
      std::bad_alloc);
  EXPECT_THROW(
      (any_small_object(std::in_place_type<sized_type<16, 16>>)),
      std::bad_alloc);
}

TEST(AnyObjectTest, UseDefaultAllocatorIfNotSpecified) {
  using any_small_object = unifex::basic_any_object<
      4,
      4,
      true,
      always_fails_allocator<std::byte>,
      get_typeid_cpo>;

  // Shouldn't throw as we have specified a non-default allocator as a
  // parameter.
  any_small_object x{
      std::allocator_arg,
      std::allocator<std::byte>(),
      std::in_place_type<sized_type<32, 4>>};
  EXPECT_TRUE((get_typeid(x) == unifex::type_id<sized_type<32, 4>>()));

  // Should throw since it will fall-back to using the default allocator which
  // always throws.
  EXPECT_THROW(
      (any_small_object(std::in_place_type<sized_type<32, 4>>)),
      std::bad_alloc);
}

namespace
{
  [[maybe_unused]] inline constexpr struct get_foo_cpo {
    using type_erased_signature_t = int(const unifex::this_&);

    template(typename T)                                         //
        (requires unifex::tag_invocable<get_foo_cpo, const T&>)  //
        auto
        operator()(const T& x) const
        noexcept(unifex::is_nothrow_tag_invocable_v<get_foo_cpo, const T&>)
            -> unifex::tag_invoke_result_t<get_foo_cpo, const T&> {
      return unifex::tag_invoke(*this, x);
    }
  } get_foo{};

  using any_foo = unifex::
      basic_any_object<16, 16, true, std::allocator<std::byte>, get_foo_cpo>;

  struct foo_supported {
    int foo = 0;

    friend int tag_invoke(get_foo_cpo, const foo_supported& x) noexcept {
      return x.foo;
    }
  };

}  // namespace

// Check that the constructors SFINAE out appropriately when we ask if
// we can implicitly/explicitly.
static_assert(std::is_constructible_v<any_foo, foo_supported>);
static_assert(std::is_convertible_v<foo_supported, any_foo>);
static_assert(!std::is_convertible_v<int, any_foo>);

// Some other checks about the nature of the type-erased wrapper.
static_assert(unifex::callable<get_foo_cpo, const any_foo&>);
static_assert(std::is_nothrow_move_constructible_v<any_foo>);
static_assert(!std::is_copy_constructible_v<any_foo>);

TEST(AnyObjectTest, ConvertibleConstructor) {
  any_foo foo = foo_supported{20};
  EXPECT_EQ(get_foo(foo), 20);
}

TEST(AnyObjectTest, MoveAssignmentDoesntDestroyRhs) {
  instance_counter::reset_counts();

  using any_t = unifex::any_object_t<get_typeid, to_string>;

  {
    any_t x{std::in_place_type<instance_counter>};
    {
      any_t y{std::in_place_type<instance_counter>};

      EXPECT_EQ(instance_counter::get_constructor_count(), 2);
      EXPECT_EQ(instance_counter::get_destructor_count(), 0);

      EXPECT_EQ(to_string(x), "0 (0)");
      EXPECT_EQ(to_string(y), "1 (1)");

      // This should destroy LHS before then constructing a new object in-place.
      x = std::move(y);

      EXPECT_EQ(instance_counter::get_constructor_count(), 3);
      EXPECT_EQ(instance_counter::get_destructor_count(), 1);

      EXPECT_EQ(to_string(x), "2 (1)");
      EXPECT_EQ(to_string(y), "1 (-1)");
    }

    EXPECT_EQ(instance_counter::get_constructor_count(), 3);
    EXPECT_EQ(instance_counter::get_destructor_count(), 2);
  }

  EXPECT_EQ(instance_counter::get_constructor_count(), 3);
  EXPECT_EQ(instance_counter::get_destructor_count(), 3);
}

TEST(AnyObjectTest, SelfMoveAssignmentDoesNothing) {
  instance_counter::reset_counts();

  using any_t = unifex::any_object_t<get_typeid, to_string>;

  {
    any_t x{std::in_place_type<instance_counter>};

    auto& xAlias = x;
    
    EXPECT_EQ(instance_counter::get_constructor_count(), 1);
    EXPECT_EQ(instance_counter::get_destructor_count(), 0);

    xAlias = std::move(x);

    EXPECT_EQ(instance_counter::get_constructor_count(), 1);
    EXPECT_EQ(instance_counter::get_destructor_count(), 0);
  }

  EXPECT_EQ(instance_counter::get_constructor_count(), 1);
  EXPECT_EQ(instance_counter::get_destructor_count(), 1);
}

namespace
{
  struct big_instance_counter : instance_counter {
    std::byte bytes[100];
  };
}  // namespace

TEST(AnyObjectTest, MoveAssignmentFromHeapAllocatedValue1) {
  instance_counter::reset_counts();

  using any_t = unifex::any_object_t<get_typeid, to_string>;

  // Test assigning to an instance that has a different type.
  {
    any_t x{std::in_place_type<instance_counter>};

    {
      // This should be heap-allocated
      any_t y{std::in_place_type<big_instance_counter>};
      EXPECT_EQ(instance_counter::get_constructor_count(), 2);
      EXPECT_EQ(instance_counter::get_destructor_count(), 0);

      x = std::move(y);

      EXPECT_EQ(instance_counter::get_constructor_count(), 2);
      EXPECT_EQ(instance_counter::get_destructor_count(), 1);
    }

    EXPECT_EQ(instance_counter::get_constructor_count(), 2);
    EXPECT_EQ(instance_counter::get_destructor_count(), 1);
  }

  EXPECT_EQ(instance_counter::get_constructor_count(), 2);
  EXPECT_EQ(instance_counter::get_destructor_count(), 2);
}

TEST(AnyObjectTest, MoveAssignmentFromHeapAllocatedValue2) {
  instance_counter::reset_counts();

  using any_t = unifex::any_object_t<get_typeid, to_string>;

  // Test assigning to an instance that has a different type.
  {
    any_t x{std::in_place_type<big_instance_counter>};

    EXPECT_EQ(to_string(x), "0 (0)");

    {
      // This should be heap-allocated
      any_t y{std::in_place_type<big_instance_counter>};
      EXPECT_EQ(instance_counter::get_constructor_count(), 2);
      EXPECT_EQ(instance_counter::get_destructor_count(), 0);
      EXPECT_EQ(to_string(y), "1 (1)");

      x = std::move(y);

      EXPECT_EQ(instance_counter::get_constructor_count(), 2);
      EXPECT_EQ(instance_counter::get_destructor_count(), 1);

      EXPECT_EQ(to_string(x), "1 (1)");
    }

    EXPECT_EQ(instance_counter::get_constructor_count(), 2);
    EXPECT_EQ(instance_counter::get_destructor_count(), 1);
    EXPECT_EQ(to_string(x), "1 (1)");
  }

  EXPECT_EQ(instance_counter::get_constructor_count(), 2);
  EXPECT_EQ(instance_counter::get_destructor_count(), 2);
}

TEST(AnyObjectTest, MoveAssignmentDifferentWrappedTypes) {
  instance_counter::reset_counts();

  using any_t = unifex::any_object_t<get_typeid, to_string>;

  {
    any_t x{std::in_place_type<instance_counter>};

    // Check assigning to an inline-stored type
    x = any_t(42);

    EXPECT_EQ(instance_counter::get_constructor_count(), 1);
    EXPECT_EQ(instance_counter::get_destructor_count(), 1);
    EXPECT_EQ(get_typeid(x), typeid(int));

    x = any_t(std::in_place_type<instance_counter>);
    EXPECT_EQ(instance_counter::get_constructor_count(), 3);
    EXPECT_EQ(instance_counter::get_destructor_count(), 2);
  }

  EXPECT_EQ(instance_counter::get_constructor_count(), 3);
  EXPECT_EQ(instance_counter::get_destructor_count(), 3);
}

TEST(AnyObjectTest, MoveAssignmentHeapAllocated) {
  instance_counter::reset_counts();

  {
    any_typeidable x{std::in_place_type<big_instance_counter>};
    EXPECT_EQ(instance_counter::get_constructor_count(), 1);
    EXPECT_EQ(instance_counter::get_destructor_count(), 0);
  }
}

namespace
{
  struct allocation {
    explicit allocation(void* p, size_t sz) noexcept : pointer(p), size(sz) {}
    void* pointer;
    size_t size;
  };

  struct tracking_allocator_base {
  public:
    static std::vector<allocation> get_allocations() { return allocations; }

  protected:
    static std::vector<allocation> allocations;
  };

  inline std::vector<allocation> tracking_allocator_base::allocations{};

  template <typename T>
  struct tracking_allocator : tracking_allocator_base {
    using value_type = T;

    tracking_allocator() = default;

    template <typename OtherT>
    tracking_allocator(tracking_allocator<OtherT>) noexcept {}

    T* allocate(size_t n) {
      T* result = std::allocator<T>{}.allocate(n);
      tracking_allocator_base::allocations.emplace_back(result, n * sizeof(T));
      return result;
    }

    void deallocate(T* ptr, size_t n) {
      auto it = tracking_allocator_base::allocations.begin();
      while (it != tracking_allocator_base::allocations.end()) {
        if (it->pointer == ptr && it->size == n * sizeof(T)) {
          tracking_allocator_base::allocations.erase(it);
          goto free;
        }
      }

      assert(false && "deallocating unrecognised allocation");

free:
      std::allocator<T>{}.deallocate(ptr, n);
    }
  };

  struct ThrowingMove : instance_counter {
    ThrowingMove() noexcept = default;

    ThrowingMove(ThrowingMove&& x) noexcept(false)
      : instance_counter(std::move(x)) {
      throw std::logic_error("shouldn't be called");
    }

    friend std::string
    tag_invoke(unifex::tag_t<to_string>, const ThrowingMove&) {
      return "ThrowingMove";
    }
  };
}  // namespace

TEST(AnyObjectTest, TypeEraseTypeWithThrowingMoveConstructorHeapAllocates) {
  using any_t = unifex::basic_any_object_t<
      sizeof(ThrowingMove),
      alignof(ThrowingMove),
      true,
      tracking_allocator<std::byte>,
      to_string,
      get_typeid>;

  instance_counter::reset_counts();

  EXPECT_TRUE(tracking_allocator_base::get_allocations().empty());

  {
    // As the any_t type requires a nothrow move-constructor but the type
    // we are constructing in there has a potentially-throwing move
    // constructor, we
    any_t x{std::in_place_type<ThrowingMove>};
    EXPECT_TRUE(get_typeid(x) == unifex::type_id<ThrowingMove>());

    auto allocs = tracking_allocator_base::get_allocations();
    EXPECT_EQ(allocs.size(), 1);

    EXPECT_GE(allocs[0].size, sizeof(ThrowingMove));
    EXPECT_NE(allocs[0].pointer, nullptr);

    EXPECT_EQ(instance_counter::get_constructor_count(), 1);
    EXPECT_EQ(instance_counter::get_destructor_count(), 0);

    any_t y = std::move(x);

    auto allocs2 = tracking_allocator_base::get_allocations();
    EXPECT_EQ(allocs2.size(), 1);

    EXPECT_EQ(allocs2[0].size, allocs[0].size);
    EXPECT_EQ(allocs[0].pointer, allocs[0].pointer);

    EXPECT_EQ(instance_counter::get_constructor_count(), 1);
    EXPECT_EQ(instance_counter::get_destructor_count(), 0);
  }

  auto allocs = tracking_allocator_base::get_allocations();
  EXPECT_TRUE(allocs.empty());
}

TEST(
    AnyObjectTest,
    TypeEraseTypeWithThrowingMoveConstructorStackAllocatesIfNoexceptMoveNotRequired) {
  using any_t = unifex::basic_any_object_t<
      sizeof(ThrowingMove),
      alignof(ThrowingMove),
      false,  // Move constructor not required to be noexcept
      tracking_allocator<std::byte>,
      to_string,
      get_typeid>;

  instance_counter::reset_counts();

  EXPECT_TRUE(tracking_allocator_base::get_allocations().empty());

  {
    // As the any_t type doesn't require a nothrow move-constructor
    // this should construct inline and avoid a heap-allocation.
    any_t x{std::in_place_type<ThrowingMove>};
    EXPECT_TRUE(get_typeid(x) == unifex::type_id<ThrowingMove>());

    auto allocs = tracking_allocator_base::get_allocations();
    EXPECT_TRUE(allocs.empty());

    EXPECT_EQ(instance_counter::get_constructor_count(), 1);
    EXPECT_EQ(instance_counter::get_destructor_count(), 0);

    any_t y = 42;

    // Attempting to move-assign/move-construct will end up constructing
    // and immediately destructing the instance_counter when the ThrowingMove
    // move-constructor runs.
    EXPECT_THROW(y = std::move(x), std::logic_error);

    auto allocs2 = tracking_allocator_base::get_allocations();
    EXPECT_TRUE(allocs2.empty());

    EXPECT_EQ(instance_counter::get_constructor_count(), 2);
    EXPECT_EQ(instance_counter::get_destructor_count(), 1);
  }

  auto allocs3 = tracking_allocator_base::get_allocations();
  EXPECT_TRUE(allocs3.empty());

  EXPECT_EQ(instance_counter::get_constructor_count(), 2);
  EXPECT_EQ(instance_counter::get_destructor_count(), 2);
}

#include <unifex/detail/epilogue.hpp>
