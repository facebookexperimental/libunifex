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
#pragma once

#include <unifex/config.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

#if (defined(__cpp_lib_type_trait_variable_templates) && \
  __cpp_lib_type_trait_variable_templates > 0)
#define UNIFEX_CXX_TRAIT_VARIABLE_TEMPLATES 1
#else
#define UNIFEX_CXX_TRAIT_VARIABLE_TEMPLATES 0
#endif

#if defined(__clang__)
#define UNIFEX_IS_SAME(...) __is_same(__VA_ARGS__)
#elif defined(__GNUC__) && __GNUC__ >= 6
#define UNIFEX_IS_SAME(...) __is_same_as(__VA_ARGS__)
#elif UNIFEX_CXX_TRAIT_VARIABLE_TEMPLATES
#define UNIFEX_IS_SAME(...) std::is_same_v<__VA_ARGS__>
#else
#define UNIFEX_IS_SAME(...) std::is_same<__VA_ARGS__>::value
#endif

#if defined(__GNUC__) || defined(_MSC_VER)
#define UNIFEX_IS_BASE_OF(...) __is_base_of(__VA_ARGS__)
#elif UNIFEX_CXX_TRAIT_VARIABLE_TEMPLATES
#define UNIFEX_IS_BASE_OF(...) std::is_base_of_v<__VA_ARGS__>
#else
#define UNIFEX_IS_BASE_OF(...) std::is_base_of<__VA_ARGS__>::value
#endif

#if defined(__clang__) || defined(_MSC_VER) || \
  (defined(__GNUC__) && __GNUC__ >= 8)
#define UNIFEX_IS_CONSTRUCTIBLE(...) __is_constructible(__VA_ARGS__)
#elif UNIFEX_CXX_TRAIT_VARIABLE_TEMPLATES
#define UNIFEX_IS_CONSTRUCTIBLE(...) std::is_constructible_v<__VA_ARGS__>
#else
#define UNIFEX_IS_CONSTRUCTIBLE(...) std::is_constructible<__VA_ARGS__>::value
#endif

#define UNIFEX_DECLVAL(...) static_cast<__VA_ARGS__(*)()noexcept>(nullptr)()

namespace unifex {

template <typename T>
struct type_identity {
  using type = T;
};

template<typename T>
using type_identity_t = typename type_identity<T>::type;

template <typename... Ts>
struct single_type {};

template <typename T>
struct single_type<T> {
  using type = T;
};

template <typename... Ts>
using single_type_t = typename single_type<Ts...>::type;

// We don't care about volatile, and not handling volatile is
// less work for the compiler.
template <class T> struct remove_cvref { using type = T; };
template <class T> struct remove_cvref<T const> { using type = T; };
// template <class T> struct remove_cvref<T volatile> { using type = T; };
// template <class T> struct remove_cvref<T const volatile> { using type = T; };
template <class T> struct remove_cvref<T&> { using type = T; };
template <class T> struct remove_cvref<T const&> { using type = T; };
// template <class T> struct remove_cvref<T volatile&> { using type = T; };
// template <class T> struct remove_cvref<T const volatile&> { using type = T; };
template <class T> struct remove_cvref<T&&> { using type = T; };
template <class T> struct remove_cvref<T const&&> { using type = T; };
// template <class T> struct remove_cvref<T volatile&&> { using type = T; };
// template <class T> struct remove_cvref<T const volatile&&> { using type = T; };

template <class T> using remove_cvref_t = typename remove_cvref<T>::type;

template <template <typename...> class T, typename X>
inline constexpr bool instance_of_v = false;

template <template <typename...> class T, typename... Args>
inline constexpr bool instance_of_v<T, T<Args...>> = true;

template <template <typename...> class T, typename X>
using instance_of = std::bool_constant<instance_of_v<T, X>>;

namespace _unit {
struct unit {};
}
using _unit::unit;

template <bool B>
struct _if {
  template <typename, typename T>
  using apply = T;
};
template <>
struct _if<true> {
  template <typename T, typename>
  using apply = T;
};

template <bool B, typename T, typename U>
using conditional_t = typename _if<B>::template apply<T, U>;

template <typename T>
using non_void_t = conditional_t<std::is_void_v<T>, unit, T>;

template <typename T>
using wrap_reference_t = conditional_t<
    std::is_reference_v<T>,
    std::reference_wrapper<std::remove_reference_t<T>>,
    T>;

template <class Member, class Self>
Member Self::*_memptr(const Self&);

template <typename Self, typename Member>
using member_t = decltype(
    (UNIFEX_DECLVAL(Self&&) .*
        unifex::_memptr<Member>(UNIFEX_DECLVAL(Self&&))));

template <typename T>
using decay_rvalue_t =
    conditional_t<std::is_lvalue_reference_v<T>, T, remove_cvref_t<T>>;

template <typename... Args>
using is_empty_list = std::bool_constant<(sizeof...(Args) == 0)>;

template <typename T>
struct is_nothrow_constructible_from {
  template <typename... Args>
  using apply = std::is_nothrow_constructible<T, Args...>;
};

template <template <typename...> class Tuple>
struct decayed_tuple {
  template <typename... Ts>
  using apply = Tuple<remove_cvref_t<Ts>...>;
};

template <typename T, typename... Ts>
inline constexpr bool is_one_of_v = (UNIFEX_IS_SAME(T, Ts) || ...);

template <typename Fn, typename... As>
using callable_result_t =
    decltype(UNIFEX_DECLVAL(Fn&&)(UNIFEX_DECLVAL(As&&)...));

namespace _is_callable {
  struct yes_type { char dummy; };
  struct no_type { char dummy[2]; };
  static_assert(sizeof(yes_type) != sizeof(no_type));

  template <
      typename Fn,
      typename... As,
      typename = callable_result_t<Fn, As...>>
  yes_type _try_call(Fn(*)(As...))
      noexcept(noexcept(UNIFEX_DECLVAL(Fn&&)(UNIFEX_DECLVAL(As&&)...)));
  no_type _try_call(...) noexcept(false);
} // namespace _is_callable

template <typename Fn, typename... As>
inline constexpr bool is_callable_v =
  sizeof(decltype(_is_callable::_try_call(static_cast<Fn(*)(As...)>(nullptr)))) == sizeof(_is_callable::yes_type);

template <typename Fn, typename... As>
struct is_callable : std::bool_constant<is_callable_v<Fn, As...>> {};

template <typename Fn, typename... As>
inline constexpr bool is_nothrow_callable_v =
    noexcept(_is_callable::_try_call(static_cast<Fn(*)(As...)>(nullptr)));

template <typename Fn, typename... As>
struct is_nothrow_callable : std::bool_constant<is_nothrow_callable_v<Fn, As...>> {};

template <typename T>
struct type_always {
  template <typename...>
  using apply = T;
};

template <template <typename...> class T>
struct meta_quote {
  template <typename... As>
  struct bind_front {
    template <typename... Bs>
    using apply = T<As..., Bs...>;
  };
  template <typename... As>
  using apply = T<As...>;
};

template <template <typename> class T>
struct meta_quote1 {
  template <typename...>
  struct bind_front;
  template <typename A0>
  struct bind_front<A0> {
    template <int = 0>
    using apply = T<A0>;
  };
  template <typename A0>
  using apply = T<A0>;
};

template <template <typename, typename> class T>
struct meta_quote2 {
  template <typename...>
  struct bind_front;
  template <typename A0>
  struct bind_front<A0> {
    template <typename A1>
    using apply = T<A0, A1>;
  };
  template <typename A0, typename A1>
  struct bind_front<A0, A1> {
    template <int = 0>
    using apply = T<A0, A1>;
  };
  template <typename A0, typename A1>
  using apply = T<A0, A1>;
};

template <template <typename, typename, typename> class T>
struct meta_quote3 {
  template <typename...>
  struct bind_front;
  template <typename A0>
  struct bind_front<A0> {
    template <typename A1, typename A2>
    using apply = T<A0, A1, A2>;
  };
  template <typename A0, typename A1>
  struct bind_front<A0, A1> {
    template <typename A2>
    using apply = T<A0, A1, A2>;
  };
  template <typename A0, typename A1, typename A2>
  struct bind_front<A0, A1, A2> {
    template <int = 0>
    using apply = T<A0, A1, A2>;
  };
  template <typename A0, typename A1, typename A2>
  using apply = T<A0, A1, A2>;
};

template <template <typename, typename...> class T>
struct meta_quote1_ {
  template <typename A, typename... Bs>
  using apply = T<A, Bs...>;
  template <typename...>
  struct bind_front;
  template <typename A, typename... Bs>
  struct bind_front<A, Bs...> {
    template <typename... Cs>
    using apply = T<A, Bs..., Cs...>;
  };
};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
