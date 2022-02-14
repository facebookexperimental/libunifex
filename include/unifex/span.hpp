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
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <unifex/std_concepts.hpp>
#include <unifex/detail/prologue.hpp>

namespace unifex {

inline constexpr std::size_t dynamic_extent = static_cast<std::size_t>(-1);

template <typename T, std::size_t Extent = dynamic_extent>
struct span {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = std::add_lvalue_reference_t<T>;
  using pointer = std::add_pointer_t<T>;

  // QUESTION: Should we really have this constructor?
  constexpr span() noexcept : data_(nullptr) {}

  explicit constexpr span(T* data) noexcept : data_(data) {}

  explicit constexpr span(const span<T, dynamic_extent>& other) noexcept
      : data_(other.data()) {
    UNIFEX_ASSERT(other.size() >= Extent);
  }

  template <std::size_t N>
  constexpr span(T (&arr)[N]) noexcept : data_(&arr[0]) {
    static_assert(N >= Extent);
  }

  template <std::size_t N>
  constexpr span(std::array<T, N>& arr) noexcept : data_(arr.data()) {
    static_assert(N >= Extent);
  }

  template <std::size_t OtherExtent>
  constexpr span(const span<T, OtherExtent>& other) noexcept
      : data_(other.data()) {
    static_assert(
        OtherExtent >= Extent,
        "Cannot construct a larger span from a smaller one");
  }

  template(typename U)
    (requires (!std::is_const_v<U>) AND same_as<const U, T>)
  explicit constexpr span(const span<U, dynamic_extent>& other) noexcept
      : data_(other.data()) {
    UNIFEX_ASSERT(other.size() >= Extent);
  }

  template(std::size_t OtherExtent, typename U)
      (requires (!std::is_const_v<U>) AND same_as<const U, T>)
  constexpr span(const span<U, OtherExtent>& other) noexcept
      : data_(other.data()) {
    static_assert(
        OtherExtent >= Extent,
        "Cannot construct a larger span from a smaller one");
  }

  T& operator[](std::size_t index) const noexcept {
    UNIFEX_ASSERT(index < size());
    return data_[index];
  }

  constexpr T* data() const noexcept {
    return data_;
  }

  constexpr std::size_t size() const noexcept {
    return Extent;
  }

  constexpr bool empty() const noexcept {
    return Extent == 0;
  }

  T* begin() const noexcept {
    return data();
  }

  T* end() const noexcept {
    return data() + size();
  }

  template <std::size_t N>
  span<T, N> first() const noexcept {
    static_assert(N != dynamic_extent);
    static_assert(
        N <= Extent,
        "Cannot slide to more elements than were in original span");
    return span<T, N>{data_};
  }

  span<T, dynamic_extent> first(std::size_t count) const noexcept;

  template <std::size_t N>
  span<T, N> last() const noexcept {
    static_assert(N != dynamic_extent);
    static_assert(
        N <= Extent,
        "Cannot slide to more elements than were in original span");
    return span<T, N>{data_ + (Extent - N)};
  }

  span<T, dynamic_extent> last(std::size_t count) const noexcept;

  template <std::size_t N>
  span<T, Extent - N> after() const noexcept {
    static_assert(N != dynamic_extent);
    static_assert(
        N <= Extent,
        "Cannot slice to more elements than were in original span");
    return span<T, Extent - N>{data_ + N};
  }

  span<T, dynamic_extent> after(std::size_t count) const noexcept;

 private:
  T* data_;
};

template <typename T>
struct span<T, dynamic_extent> {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = std::add_lvalue_reference_t<T>;
  using pointer = std::add_pointer_t<T>;

  constexpr span() noexcept : data_(nullptr), size_(0) {}

  constexpr span(T* data, std::size_t size) noexcept
      : data_(data), size_(size) {}

  template <std::size_t N>
  constexpr span(T (&arr)[N]) noexcept : data_(arr), size_(N) {}

  template <std::size_t N>
  constexpr span(std::array<T, N>& arr) noexcept
      : data_(arr.data()), size_(N) {}

  template(typename U, std::size_t OtherExtent)
      (requires same_as<U, T> ||
          (!std::is_const_v<U> && same_as<const U, T>))
  constexpr span(const span<U, OtherExtent>& other) noexcept
      : data_(other.data()), size_(other.size()) {}

  T& operator[](std::size_t index) const noexcept {
    UNIFEX_ASSERT(index < size());
    return data_[index];
  }

  T* data() const noexcept {
    return data_;
  }

  bool empty() const noexcept {
    return size_ == 0;
  }

  std::size_t size() const noexcept {
    return size_;
  }

  T* begin() const noexcept {
    return data();
  }

  T* end() const noexcept {
    return data() + size();
  }

  template <std::size_t N>
  span<T, N> first() const noexcept {
    static_assert(N != dynamic_extent);
    UNIFEX_ASSERT(
        N <= size() &&
        "Cannot slice to more elements than were in original span");
    return span<T, N>{data()};
  }

  span<T, dynamic_extent> first(std::size_t count) const noexcept {
    UNIFEX_ASSERT(
        count <= size() &&
        "Cannot slice to more elements than were in original span");
    return span<T, dynamic_extent>{data(), count};
  }

  template <std::size_t N>
  span<T, N> last() const noexcept {
    static_assert(N != dynamic_extent);
    UNIFEX_ASSERT(
        N <= size() &&
        "Cannot slice to more elements than were in original span");
    return span<T, N>{data() + (size() - N)};
  }

  span<T, dynamic_extent> last(std::size_t count) const noexcept {
    UNIFEX_ASSERT(
        count <= size() &&
        "Cannot slice to more elements than were in original span");
    return span<T, dynamic_extent>{data() + (size() - count), count};
  }

  template <std::size_t N>
  span<T, dynamic_extent> after() const noexcept {
    static_assert(N != dynamic_extent);
    UNIFEX_ASSERT(
        N <= size() &&
        "Cannot slice to more elements than were in original span");
    return span<T, dynamic_extent>{data() + N, size() - N};
  }

  span<T, dynamic_extent> after(std::size_t count) const noexcept {
    UNIFEX_ASSERT(
        count <= size() &&
        "Cannot slice to more elements than were in original span");
    return span<T, dynamic_extent>{data() + count, size() - count};
  }

 private:
  T* data_;
  std::size_t size_;
};

template <typename T, std::size_t Extent>
inline span<T, dynamic_extent> span<T, Extent>::first(std::size_t count) const
    noexcept {
  UNIFEX_ASSERT(count <= Extent);
  return span<T, dynamic_extent>{data(), count};
}

template <typename T, std::size_t Extent>
inline span<T, dynamic_extent> span<T, Extent>::last(std::size_t count) const
    noexcept {
  UNIFEX_ASSERT(count <= Extent);
  return span<T, dynamic_extent>{data() + (Extent - count), count};
}

template <typename T, std::size_t Extent>
inline span<T, dynamic_extent> span<T, Extent>::after(std::size_t count) const
    noexcept {
  UNIFEX_ASSERT(count <= Extent);
  return span<T, dynamic_extent>{data() + count, Extent - count};
}

template <typename T, std::size_t N>
span(T (&)[N])->span<T, N>;

template <typename T, std::size_t N>
span(std::array<T, N>&)->span<T, N>;

template <typename T, std::size_t N>
span(const std::array<T, N>&)->span<const T, N>;

template <typename T>
span(T*, std::size_t)->span<T>;

template <typename T, std::size_t Extent>
span<const std::byte, Extent * sizeof(T)> as_bytes(
    const span<T, Extent>& s) noexcept {
  constexpr std::size_t maxSize = std::size_t(-1) / sizeof(T);
  static_assert(Extent <= maxSize);
  return span<const std::byte, Extent * sizeof(T)>{
      reinterpret_cast<const std::byte*>(s.data())};
}

template <typename T>
span<const std::byte> as_bytes(const span<T>& s) noexcept {
  [[maybe_unused]] constexpr std::size_t maxSize = std::size_t(-1) / sizeof(T);
  UNIFEX_ASSERT(s.size() <= maxSize);
  return span<const std::byte>{reinterpret_cast<const std::byte*>(s.data()),
                               s.size() * sizeof(T)};
}

template(typename T, std::size_t Extent)
    (requires (!std::is_const_v<T>))
span<std::byte, Extent * sizeof(T)> as_writable_bytes(
    const span<T, Extent>& s) noexcept {
  constexpr std::size_t maxSize = std::size_t(-1) / sizeof(T);
  static_assert(Extent <= maxSize);
  return span<std::byte, Extent * sizeof(T)>{
      reinterpret_cast<std::byte*>(s.data())};
}

template(typename T)
    (requires (!std::is_const_v<T>))
span<std::byte> as_writable_bytes(const span<T>& s) noexcept {
  [[maybe_unused]] constexpr std::size_t maxSize = std::size_t(-1) / sizeof(T);
  UNIFEX_ASSERT(s.size() <= maxSize);
  return span<std::byte>{reinterpret_cast<std::byte*>(s.data()),
                         s.size() * sizeof(T)};
}

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
