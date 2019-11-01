#pragma once

#include <type_traits>
#include <functional>
#include <memory>
#include <new>

namespace unifex {

template <typename T>
class manual_lifetime {
 public:
  manual_lifetime() noexcept {}
  ~manual_lifetime() {}

  template <typename... Args>
  T& construct(Args&&... args) noexcept(
      std::is_nothrow_constructible_v<T, Args...>) {
    return *::new (static_cast<void*>(std::addressof(value_)))
        T((Args &&) args...);
  }

  template <typename Func>
  T& construct_from(Func&& func) noexcept(noexcept(std::invoke((Func &&)
                                                                   func))) {
    static_assert(
        std::is_same_v<std::invoke_result_t<Func>, T>,
        "Return type of func() must be exactly T to permit copy-elision.");
    return *::new (static_cast<void*>(std::addressof(value_)))
        T(std::invoke((Func &&) func));
  }

  void destruct() noexcept(std::is_nothrow_destructible_v<T>) {
    value_.~T();
  }

  T& get() & noexcept {
    return value_;
  }
  T&& get() && noexcept {
    return (T &&) value_;
  }
  const T& get() const& noexcept {
    return value_;
  }
  const T&& get() const&& noexcept {
    return (const T&&)value_;
  }

 private:
  union {
    T value_;
  };
};

template <typename T>
class manual_lifetime<T&> {
 public:
  manual_lifetime() noexcept : value_(nullptr) {}
  ~manual_lifetime() {}

  T& construct(T& value) noexcept {
    value_ = std::addressof(value);
    return value;
  }

  template <typename Func>
  T& construct_from(Func&& func) noexcept(noexcept(std::invoke((Func &&)
                                                                   func))) {
    static_assert(std::is_same_v<std::invoke_result_t<Func>, T&>);
    value_ = std::invoke((Func &&) func);
    return value_;
  }

  void destruct() noexcept {}

  T& get() const noexcept {
    return *value_;
  }

 private:
  T* value_;
};

template <typename T>
class manual_lifetime<T&&> {
 public:
  manual_lifetime() noexcept : value_(nullptr) {}
  ~manual_lifetime() {}

  T&& construct(T&& value) noexcept {
    value_ = std::addressof(value);
    return (T &&) value;
  }

  template <typename Func>
  T&& construct_from(Func&& func) noexcept(noexcept(std::invoke((Func &&)
                                                                    func))) {
    static_assert(std::is_same_v<std::invoke_result_t<Func>, T&&>);
    value_ = std::invoke((Func &&) func);
    return (T &&) value_;
  }

  void destruct() noexcept {}

  T&& get() const noexcept {
    return (T &&) * value_;
  }

 private:
  T* value_;
};

template <>
class manual_lifetime<void> {
 public:
  manual_lifetime() noexcept = default;
  ~manual_lifetime() = default;

  void construct() noexcept {}
  template <typename Func>
  void construct_from(Func&& func) noexcept(noexcept(std::invoke((Func &&)
                                                                     func))) {
    static_assert(std::is_same_v<std::invoke_result_t<Func>, void>);
    return std::invoke((Func &&) func);
  }
  void destruct() noexcept {}
  void get() const noexcept {}
};

} // namespace unifex
