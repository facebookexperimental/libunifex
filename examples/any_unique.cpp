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
#include <unifex/any_unique.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/memory_resource.hpp>
#include <unifex/type_index.hpp>

#include <string>
#include <typeindex>
#include <atomic>

template <typename T>
using is_type_index = std::is_same<std::type_index, T>;

inline constexpr struct get_typeid_cpo {
  using type_erased_signature_t =
      unifex::type_index(const unifex::this_&) noexcept;

  template <typename T>
  friend unifex::type_index tag_invoke(get_typeid_cpo, const T&) {
    return unifex::type_id<T>();
  }

  template <typename T>
  auto operator()(const T& x) const noexcept ->
      unifex::tag_invoke_result_t<get_typeid_cpo, const T&> {
    static_assert(
      std::is_same_v<
          unifex::type_index,
          unifex::tag_invoke_result_t<get_typeid_cpo, const T&>>);
    return tag_invoke(get_typeid_cpo{}, x);
  }
} get_typeid;

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

int main() {
  using A = unifex::any_unique_t<get_typeid>;
  using B = unifex::any_unique_t<>;
  {
    const A a = std::string{"hello"};
    [[maybe_unused]] auto id = get_typeid(a);
    UNIFEX_ASSERT(id == unifex::type_id<std::string>());
  }
  {
    const B b = std::string{"hello"};
    [[maybe_unused]] auto id = get_typeid(b);
    UNIFEX_ASSERT(id == unifex::type_id<B>());
  }
  {
    bool hasDestructorRun = false;
    {
      const A a{std::in_place_type<destructor>, hasDestructorRun};
      UNIFEX_ASSERT(get_typeid(a) == unifex::type_id<destructor>());
      UNIFEX_ASSERT(!hasDestructorRun);
    }
    UNIFEX_ASSERT(hasDestructorRun);
  }

#if !UNIFEX_NO_MEMORY_RESOURCE
  {
    counting_memory_resource res{new_delete_resource()};
    polymorphic_allocator<char> alloc{&res};
    {
      A a1{std::string("hello"), alloc};
      UNIFEX_ASSERT(res.total_allocated_bytes() >= sizeof(std::string));
      A a2{std::allocator_arg, alloc, std::in_place_type<std::string>, "hello"};
      UNIFEX_ASSERT(res.total_allocated_bytes() >= 2 * sizeof(std::string));
    }
    UNIFEX_ASSERT(res.total_allocated_bytes() == 0);
  }
#endif
  return 0;
}
