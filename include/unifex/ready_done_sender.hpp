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

#include <unifex/blocking.hpp>
#include <unifex/config.hpp>
#include <unifex/receiver_concepts.hpp>

#include <type_traits>

namespace unifex {

struct ready_done_sender {
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  friend constexpr blocking_kind tag_invoke(
      tag_t<cpo::blocking>,
      const ready_done_sender&) {
    return blocking_kind::always_inline;
  }

  template <typename Receiver>
  struct operation {
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    void start() noexcept {
      cpo::set_done(static_cast<Receiver&&>(receiver_));
    }
  };

  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) {
    return operation<std::remove_cvref_t<Receiver>>{(Receiver &&) receiver};
  }
};

} // namespace unifex
