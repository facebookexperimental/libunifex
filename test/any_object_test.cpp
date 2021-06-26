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

#include <gtest/gtest.h>

#include <unifex/detail/prologue.hpp>

static_assert(unifex::is_callable_v<unifex::detail::_destroy_cpo, int&>);
static_assert(unifex::detail::supports_type_erased_cpo_v<
              int,
              unifex::detail::_destroy_cpo>);

namespace
{
  inline constexpr struct get_typeid_cpo {
    using type_erased_signature_t =
        unifex::type_index(const unifex::this_&) noexcept;

    template <typename T>
    friend unifex::type_index tag_invoke(get_typeid_cpo, const T& x) {
      return unifex::type_id<T>();
    }

    template(typename T)                                     //
        (requires unifex::tag_invocable<get_typeid_cpo, T>)  //
        auto
        operator()(const T& x) const noexcept
        -> unifex::tag_invoke_result_t<get_typeid_cpo, const T&> {
      static_assert(std::is_same_v<
                    unifex::type_index,
                    unifex::tag_invoke_result_t<get_typeid_cpo, const T&>>);
      return unifex::tag_invoke(get_typeid_cpo{}, x);
    }
  } get_typeid{};

  using any_typeidable = unifex::any_object<
      8,
      8,
      true,
      std::allocator<std::byte>,
      unifex::tag_t<get_typeid>>;

  struct instance_counter {
  private:
    static size_t constructor_count;
    static size_t destructor_count;

  public:
    size_t id;
    size_t original_id;

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
      , original_id(x.original_id) {}
    ~instance_counter() { ++destructor_count; }

    instance_counter& operator=(const instance_counter& x) noexcept {
      original_id = x.original_id;
      return *this;
    }
  };

  size_t instance_counter::constructor_count = 0;
  size_t instance_counter::destructor_count = 0;
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
    any_typeidable x{std::in_place_type<float>, 42};
    EXPECT_TRUE(get_typeid(x) == unifex::type_id<float>());
  }
}

TEST(AnyObjectTest, InPlaceConstructionOnlyConstructsOnce) {
  instance_counter::reset_counts();

  {
    any_typeidable x{std::in_place_type<instance_counter>};
    EXPECT_EQ(instance_counter::get_constructor_count(), 1);
    EXPECT_EQ(instance_counter::get_instance_count(), 1);
  }

  EXPECT_EQ(instance_counter::get_instance_count(), 0);
  EXPECT_EQ(instance_counter::get_destructor_count(), 1);
}

TEST(AnyObjectTest, MoveConstructionMovesSmallObjects) {
  instance_counter::reset_counts();

  using any_small_object = unifex::any_object<
      sizeof(instance_counter),
      alignof(instance_counter),
      true,
      std::allocator<std::byte>,
      unifex::tag_t<get_typeid>>;

  {
    any_small_object x{std::in_place_type<instance_counter>};

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

  using any_small_object = unifex::any_object<
      sizeof(instance_counter),
      alignof(instance_counter),
      true,
      std::allocator<std::byte>,
      unifex::tag_t<get_typeid>>;

  {
    any_small_object x{std::in_place_type<big_instance_counter>};

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

    T* allocate(size_t n) { throw std::bad_alloc{}; }

    void deallocate(T*, size_t n) { std::terminate(); }
  };

  template <size_t Size, size_t Alignment>
  struct alignas(Alignment) sized_type {
    std::byte storage[Size];
  };

}  // namespace

TEST(AnyObjectTest, SmallObjectsDontCallAllocator) {
  using any_small_object = unifex::any_object<
      16,
      8,
      true,
      always_fails_allocator<std::byte>,
      get_typeid_cpo>;

  EXPECT_NO_THROW(any_small_object(std::in_place_type<sized_type<4, 4>>));
  EXPECT_NO_THROW(any_small_object(std::in_place_type<sized_type<8, 4>>));
  EXPECT_NO_THROW(any_small_object(std::in_place_type<sized_type<16, 4>>));
  EXPECT_NO_THROW(any_small_object(std::in_place_type<sized_type<4, 8>>));
  EXPECT_NO_THROW(any_small_object(std::in_place_type<sized_type<16, 8>>));
}

TEST(AnyObjectTest, LargeObjectsCallAllocator) {
  using any_small_object = unifex::any_object<
      16,
      8,
      true,
      always_fails_allocator<std::byte>,
      get_typeid_cpo>;

  EXPECT_THROW(
      any_small_object(std::in_place_type<sized_type<32, 4>>), std::bad_alloc);
  EXPECT_THROW(
      any_small_object(std::in_place_type<sized_type<16, 16>>), std::bad_alloc);
}

TEST(AnyObjectTest, UseDefaultAllocatorIfNotSpecified) {
  using any_small_object = unifex::
      any_object<4, 4, true, always_fails_allocator<std::byte>, get_typeid_cpo>;

  // Shouldn't throw as we have specified a non-default allocator as a
  // parameter.
  EXPECT_NO_THROW(any_small_object(
      std::allocator_arg,
      std::allocator<std::byte>{},
      std::in_place_type<sized_type<32, 4>>));

  // Should throw since it will fall-back to using the default allocator which
  // always throws.
  EXPECT_THROW(
      any_small_object(std::in_place_type<sized_type<32, 4>>), std::bad_alloc);
}

namespace
{
  inline constexpr struct get_foo_cpo {
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

  using any_foo =
      unifex::any_object<16, 16, true, std::allocator<std::byte>, get_foo_cpo>;

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

#include <unifex/detail/epilogue.hpp>
