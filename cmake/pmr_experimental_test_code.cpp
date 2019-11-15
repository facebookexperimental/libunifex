// Copyright (c) 2019-present, Facebook, Inc.
//
// This source code is licensed under the Apache License found in the
// LICENSE.txt file in the root directory of this source tree.

#include <experimental/memory_resource>
#include <atomic>
using namespace std::experimental::pmr;

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

int main() {
  counting_memory_resource res{new_delete_resource()};
  polymorphic_allocator<char> alloc{&res};
  (void) alloc;
}
