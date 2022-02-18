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

#include <unifex/config.hpp>

#include <functional>
#include <type_traits>

#if defined(UNIFEX_USE_ABSEIL)
#include <absl/meta/type_traits.h>
#endif

#include <unifex/detail/prologue.hpp>

namespace unifex {

using std::is_same_v;
using std::is_void_v;
using std::is_const_v;
using std::is_empty_v;
using std::is_object_v;
using std::is_base_of_v;
using std::is_reference_v;
using std::is_convertible_v;
using std::is_lvalue_reference_v;
using std::is_nothrow_destructible_v;
using std::is_nothrow_constructible_v;
using std::is_nothrow_copy_constructible_v;
using std::is_nothrow_move_constructible_v;

#if defined(__cpp_lib_bool_constant) && \
  __cpp_lib_bool_constant > 0
using std::bool_constant;
#else
template <bool Bool>
using bool_constant = std::integral_constant<bool, Bool>;
#endif

#if defined(__cpp_lib_void_t) && __cpp_lib_void_t > 0
using std::void_t;
#else
template <typename...>
using void_t = void;
#endif

using std::invoke_result;
using std::invoke_result_t;
using std::is_invocable;
using std::is_nothrow_invocable;
using std::is_invocable_v;
using std::is_nothrow_invocable_v;

#if defined(UNIFEX_USE_ABSEIL)
using absl::disjunction;
#else
using std::disjunction;
#endif

template <std::size_t Len, class... Types>
struct aligned_union {
private:
  static constexpr std::size_t _max(std::initializer_list<std::size_t> alignments) noexcept {
    std::size_t result = 0;
    for (auto z : alignments)
      if (z > result)
        result = z;
    return result;
  }
public:
  static constexpr std::size_t alignment_value = _max({alignof(Types)...});

  struct type {
    alignas(alignment_value) char _s[_max({Len, sizeof(Types)...})];
  };
};

template <std::size_t Len, class... Types>
using aligned_union_t = typename aligned_union<Len, Types...>::type;

namespace _ti {
template <typename T>
struct type_identity {
  using type = T;
};
} // namespace _ti
using _ti::type_identity;

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
using instance_of = bool_constant<instance_of_v<T, X>>;

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
using non_void_t = conditional_t<is_void_v<T>, unit, T>;

template <typename T>
using wrap_reference_t = conditional_t<
    is_reference_v<T>,
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
    conditional_t<is_lvalue_reference_v<T>, T, remove_cvref_t<T>>;

template <typename... Args>
using is_empty_list = bool_constant<(sizeof...(Args) == 0)>;

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
struct is_callable : bool_constant<is_callable_v<Fn, As...>> {};

template <typename Fn, typename... As>
inline constexpr bool is_nothrow_callable_v =
    noexcept(_is_callable::_try_call(static_cast<Fn(*)(As...)>(nullptr)));

template <typename Fn, typename... As>
struct is_nothrow_callable : bool_constant<is_nothrow_callable_v<Fn, As...>> {};

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
