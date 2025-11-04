/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <unifex/defer.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_void_or_done.hpp>
#include <unifex/let_done.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/sequence.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>

#include <mutex>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _when_any {

template <typename Tuple, typename TypeList>
struct _compatible_with;

template <typename Tuple, typename... Result>
struct _compatible_with<Tuple, type_list<Result...>> {
  static constexpr bool value = std::is_constructible_v<Tuple, Result...>;
};

template <typename Tuple, typename TypeList>
constexpr bool compatible_with_v = _compatible_with<Tuple, TypeList>::value;

template <typename... Result, typename... Senders>
auto impl(type_list<Result...> /*unused*/, Senders&&... senders) {
  return just(
             std::optional<std::tuple<Result...>>{},
             std::forward<Senders>(senders)...) |
      let_value([](auto& optResult, auto&... senders) {
           return let_value_with(
               []() noexcept { return std::once_flag{}; },
               [&optResult, &senders...](auto& onceFlag) {
                 auto store_result = [&optResult,
                                      &onceFlag](auto&&... results) {
                   std::call_once(onceFlag, [&optResult, &results...]() {
                     optResult.emplace(
                         std::forward<decltype(results)>(results)...);
                   });
                   return just_void_or_done(false);
                 };

                 return when_all(
                            (std::move(senders) | let_value(store_result))...) |
                     let_done([&optResult]() noexcept {
                          return just_void_or_done(optResult.has_value());
                        }) |
                     let_value([&optResult](auto&&...) {
                          assert(optResult.has_value());
                          return std::apply(just, std::move(optResult.value()));
                        });
               });
         });
}

struct _fn {
  template(typename First, typename... Rest)   //
    (requires sender<First> &&                 // 
             (sender<Rest> && ...) &&          // 
             (compatible_with_v<               //  
                typename sender_value_types_t< // 
                  First,                       // 
                  single_overload,             // 
                  std::tuple>::type,           // 
                typename sender_value_types_t< // 
                  Rest,                        // 
                  single_overload,             // 
                  type_list>::type> && ...))   //  
  auto operator()(First&& first, Rest&&... rest) const {
    using ValueTypes =
        typename sender_value_types_t<First, single_overload, type_list>::type;
    return impl(
        ValueTypes{}, std::forward<First>(first), std::forward<Rest>(rest)...);
  }

  template(typename First)      //
      (requires sender<First>)  //
      First&& operator()(First&& first) const {
    return std::forward<First>(first);
  }
};

}  // namespace _when_any

inline constexpr _when_any::_fn when_any{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
