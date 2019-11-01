#pragma once

#include <cstdint>
#include <utility>

namespace unifex {
namespace linux {

struct mmap_region {
  mmap_region() noexcept : ptr_(nullptr), size_(0) {}

  mmap_region(mmap_region&& r) noexcept
      : ptr_(std::exchange(r.ptr_, nullptr)),
        size_(std::exchange(r.size_, 0)) {}

  explicit mmap_region(void* ptr, std::size_t size) noexcept
      : ptr_(ptr), size_(size) {}

  ~mmap_region();

  mmap_region& operator=(mmap_region r) noexcept {
    std::swap(ptr_, r.ptr_);
    std::swap(size_, r.size_);
    return *this;
  }

  void* data() const noexcept {
    return ptr_;
  }

  std::size_t size() const noexcept {
    return size_;
  }

 private:
  void* ptr_;
  std::size_t size_;
};

} // namespace linux
} // namespace unifex
