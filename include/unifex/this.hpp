#pragma once

#include <unifex/config.hpp>

#include <type_traits>

namespace unifex {

struct this_ {};

template <typename ArgType>
struct replace_this {
  template <typename Arg, typename T>
  using apply = Arg;

  template <typename Arg, typename T>
  static Arg&& get(Arg&& arg, T&) {
    return (Arg &&) arg;
  }
};

template <>
struct replace_this<this_> {
  template <typename Arg, typename T>
  using apply = T;

  template <typename Arg, typename T>
  static T&& get(Arg&&, T& obj) {
    return (T &&) obj;
  }
};

template <>
struct replace_this<this_&> {
  template <typename Arg, typename T>
  using apply = T&;

  template <typename Arg, typename T>
  static T& get(Arg&, T& obj) noexcept {
    return obj;
  }
};

template <>
struct replace_this<this_&&> {
  template <typename Arg, typename T>
  using apply = T&&;

  template <typename Arg, typename T>
  static T&& get(Arg&&, T& obj) noexcept {
    return (T &&) obj;
  }
};

template <>
struct replace_this<const this_&> {
  template <typename Arg, typename T>
  using apply = const T&;

  template <typename Arg, typename T>
  static const T& get(const Arg&, T& obj) noexcept {
    return obj;
  }
};

template <>
struct replace_this<const this_&&> {
  template <typename Arg, typename T>
  using type = const T&&;

  template <typename Arg, typename T>
  const T&& operator()(const Arg&&, T& obj) const {
    return (const T&&)obj;
  }
};

template <typename Arg, typename T>
using replace_this_t = typename replace_this<Arg>::template apply<Arg, T>;

template <typename Arg>
using replace_this_with_void_ptr_t = std::
    conditional_t<std::is_same_v<std::remove_cvref_t<Arg>, this_>, void*, Arg>;

template <typename... ArgTypes>
struct extract_this;

template <typename FirstType, typename... RestTypes>
struct extract_this<FirstType, RestTypes...> {
  template <typename TFirst, typename... TRest>
  decltype(auto) operator()(TFirst&& first, TRest&&... rest) const {
    if constexpr (std::is_same_v<std::remove_cvref_t<FirstType>, this_>) {
      return first;
    } else {
      static_assert(sizeof...(TRest) > 0, "Arguments to extract_this");
      return extract_this<RestTypes...>{}((TRest &&) rest...);
    }
  }
};

} // namespace unifex
