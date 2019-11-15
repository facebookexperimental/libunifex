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
#include <unifex/any_unique.hpp>
#include <unifex/type_traits.hpp>

#include <cassert>
#include <string>
#include <typeindex>
#include <atomic>

#if !UNIFEX_NO_MEMORY_RESOURCE
#include UNIFEX_MEMORY_RESOURCE_HEADER
#endif

template <typename T>
using is_type_index = std::is_same<std::type_index, T>;

inline constexpr struct get_typeid_cpo {
  using type_erased_signature_t =
      std::type_index(const unifex::this_&) noexcept;

  template <typename T>
  friend std::type_index tag_invoke(get_typeid_cpo, const T& x) {
    return typeid(x);
  }

  template <typename T>
  auto operator()(const T& x) const noexcept ->
      unifex::tag_invoke_result_t<get_typeid_cpo, const T&> {
    static_assert(
      std::is_same_v<std::type_index, unifex::tag_invoke_result_t<get_typeid_cpo, const T&>>);
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
using namespace UNIFEX_PMR_NAMESPACE;

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
    auto id = get_typeid(a);
    assert(id == typeid(std::string));
  }
  {
    const B b = std::string{"hello"};
    auto id = get_typeid(b);
    assert(id == typeid(B));
  }
  {
    bool hasDestructorRun = false;
    {
      const A a{std::in_place_type<destructor>, hasDestructorRun};
      assert(get_typeid(a) == typeid(destructor));
      assert(!hasDestructorRun);
    }
    assert(hasDestructorRun);
  }
#if !UNIFEX_NO_MEMORY_RESOURCE
  {
    counting_memory_resource res{new_delete_resource()};
    polymorphic_allocator<char> alloc{&res};
    {
      A a1{std::string("hello"), alloc};
      assert(res.total_allocated_bytes() >= sizeof(std::string));
      A a2{std::allocator_arg, alloc, std::in_place_type<std::string>, "hello"};
      assert(res.total_allocated_bytes() >= 2 * sizeof(std::string));
    }
    assert(res.total_allocated_bytes() == 0);
  }
#endif
  return 0;
}
