#pragma once

#include <unifex/manual_lifetime.hpp>

#include <type_traits>

namespace unifex {

template <typename... Ts>
class manual_lifetime_union {
 public:
  manual_lifetime_union() = default;

  template <
      typename T,
      std::enable_if_t<std::disjunction_v<std::is_same<T, Ts>...>, int> = 0>
  manual_lifetime<T>& get() noexcept {
    return *reinterpret_cast<manual_lifetime<T>*>(&storage_);
  }

 private:
  std::aligned_union_t<0, manual_lifetime<Ts>...> storage_;
};

template <>
class manual_lifetime_union<> {};

} // namespace unifex
