/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <functional>

#include <unifex/detail/concept_macros.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
#if UNIFEX_CXX_INVOKE

using std::invoke;

#else // UNIFEX_CXX_INVOKE

namespace _invoke {
struct _any {
  template <typename T>
  constexpr _any(T&&) noexcept {}
};

constexpr bool _can_deref(_any) noexcept {
  return false;
}
template <typename T>
constexpr auto _can_deref(T&& t) noexcept -> decltype(_can_deref(*(T&&) t)) {
  return true;
}

template<typename T, typename U = std::decay_t<T>>
inline constexpr bool _is_reference_wrapper_v = false;

template<typename T, typename U>
inline constexpr bool _is_reference_wrapper_v<T, std::reference_wrapper<U>> = true;

template<typename T, typename U>
inline constexpr bool _is_base_of_v = UNIFEX_IS_BASE_OF(T, U);

template(typename, typename T1)
  (requires _invoke::_can_deref<T1>(UNIFEX_DECLVAL(T1&&)))
constexpr decltype(auto) coerce(T1 && t1, long)
    noexcept(noexcept(*static_cast<T1 &&>(t1))) {
  return *static_cast<T1 &&>(t1);
}

template(typename T, typename T1)
  (requires _is_base_of_v<T, std::decay_t<T1>>)
constexpr T1 && coerce(T1 && t1, int) noexcept {
  return static_cast<T1 &&>(t1);
}

template(typename, typename T1)
  (requires _is_reference_wrapper_v<T1>)
constexpr decltype(auto) coerce(T1 && t1, int) noexcept {
  return static_cast<T1 &&>(t1).get();
}
} // _invoke

template<typename F, typename T, typename T1, typename... Args>
constexpr auto invoke(F T::*f, T1&& t1, Args&&... args)
  noexcept(noexcept((_invoke::coerce<T>((T1&&) t1, 0).*f)((Args&&) args...)))
  -> decltype((_invoke::coerce<T>((T1&&) t1, 0).*f)((Args&&) args...)) {
  return (_invoke::coerce<T>((T1&&) t1, 0).*f)((Args&&) args...);
}

template<typename D, typename T, typename T1>
constexpr auto invoke(D T::*f, T1&& t1)
  noexcept(noexcept(_invoke::coerce<T>((T1&&) t1, 0).*f))
  -> decltype(_invoke::coerce<T>((T1&&) t1, 0).*f) {
  return _invoke::coerce<T>((T1&&) t1, 0).*f;
}

template<typename F, typename... Args>
constexpr auto invoke(F&& f, Args&&... args)
  noexcept(noexcept(((F&&) f)((Args&&) args...)))
  -> decltype(((F&&) f)((Args&&) args...)) {
  return ((F&&) f)((Args&&) args...);
}

#endif // UNIFEX_CXX_INVOKE
}

#include <unifex/detail/epilogue.hpp>
