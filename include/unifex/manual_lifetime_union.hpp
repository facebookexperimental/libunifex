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
