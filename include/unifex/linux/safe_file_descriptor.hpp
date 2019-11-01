#pragma once

#include <utility>

namespace unifex {
namespace linux {

class safe_file_descriptor {
 public:
  safe_file_descriptor() noexcept : fd_(-1) {}

  explicit safe_file_descriptor(int fd) noexcept : fd_(fd) {}

  safe_file_descriptor(safe_file_descriptor&& other) noexcept
      : fd_(std::exchange(other.fd_, -1)) {}

  ~safe_file_descriptor() {
    if (valid()) {
      close();
    }
  }

  safe_file_descriptor& operator=(safe_file_descriptor other) noexcept {
    std::swap(fd_, other.fd_);
    return *this;
  }

  bool valid() const noexcept {
    return fd_ >= 0;
  }

  int get() const noexcept {
    return fd_;
  }

  void close() noexcept;

 private:
  int fd_;
};

} // namespace linux
} // namespace unifex
