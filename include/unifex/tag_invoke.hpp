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

namespace unifex {
namespace tag_invoke_impl {

void tag_invoke();

struct tag_invoke_cpo {
  template <typename CPO, typename... Args>
  constexpr auto operator()(CPO cpo, Args&&... args) const
      noexcept(noexcept(tag_invoke((CPO&&)cpo, (Args &&) args...)))
          -> decltype(tag_invoke((CPO&&)cpo, (Args &&) args...)) {
    return tag_invoke((CPO&&)cpo, (Args &&) args...);
  }
};

template<typename CPO, typename... Args>
using tag_invoke_result_t = decltype(tag_invoke(std::declval<CPO>(), std::declval<Args>()...));

template<typename CPO, typename... Args>
struct is_tag_invocable {
private:
    template<typename XCPO = CPO, typename = tag_invoke_result_t<XCPO, Args...>>
    static std::true_type try_call(int);
    static std::false_type try_call(...);
public:
    using type = decltype(try_call(0));
};

template<typename CPO, typename... Args>
struct is_nothrow_tag_invocable {
private:
    template<typename XCPO = CPO, typename = tag_invoke_result_t<XCPO, Args...>>
    static auto try_call(int) -> std::bool_constant<noexcept(tag_invoke(std::declval<XCPO>(), std::declval<Args>()...))>;
    static std::false_type try_call(...);
public:
    using type = decltype(try_call(0));
};

} // namespace tag_invoke_impl

namespace tag_invoke_cpo_ns {
inline constexpr tag_invoke_impl::tag_invoke_cpo tag_invoke{};
}
using namespace tag_invoke_cpo_ns;

template <auto& CPO>
using tag_t = std::remove_cvref_t<decltype(CPO)>;

using tag_invoke_impl::tag_invoke_result_t;

template<typename CPO, typename... Args>
struct tag_invoke_result {
    using type = tag_invoke_result_t<CPO, Args...>;
};

template <typename CPO, typename... Args>
using is_tag_invocable = typename tag_invoke_impl::is_tag_invocable<CPO, Args...>::type;

template <typename CPO, typename... Args>
inline constexpr bool is_tag_invocable_v = is_tag_invocable<CPO, Args...>::value;

template<typename CPO, typename... Args>
using is_nothrow_tag_invocable = typename tag_invoke_impl::is_nothrow_tag_invocable<CPO, Args...>::type;

template <typename CPO, typename... Args>
inline constexpr bool is_nothrow_tag_invocable_v = is_nothrow_tag_invocable<CPO, Args...>::value;

} // namespace unifex
