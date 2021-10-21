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

#include <optional>

#include <unifex/config.hpp>
#include <unifex/then.hpp>
#include <unifex/let_done.hpp>
#include <unifex/just.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/bind_back.hpp>
#include <unifex/sender_concepts.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _done_as_opt {
namespace _cpo {
inline const struct _fn {
  template(typename Sender) //
    (requires _single_typed_sender<Sender>) //
  auto operator()(Sender&& predecessor) const {
    using optional_t = std::optional<
        non_void_t<std::decay_t<sender_single_value_return_type_t<Sender>>>>;
    return let_done(
        then(
            (Sender&&) predecessor,
            [](auto&&... ts) noexcept(
                noexcept(optional_t{std::in_place, (decltype(ts)) ts...})) {
              return optional_t{std::in_place, (decltype(ts)) ts...};
            }),
        []() noexcept { return just(optional_t{}); });
  }
  constexpr auto operator()() const noexcept
      -> bind_back_result_t<_fn> {
    return bind_back(*this);
  }
} done_as_optional{};
}  // namespace _cpo
}  // namespace _done_as_opt

using _done_as_opt::_cpo::done_as_optional;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
