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

#include <unifex/blocking.hpp>
#include <unifex/config.hpp>
#include <unifex/receiver_concepts.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _ready_done {
  template <typename Receiver>
  struct _op {
    struct type;
  };
  template <typename Receiver>
  using operation = typename _op<remove_cvref_t<Receiver>>::type;

  template <typename Receiver>
  struct _op<Receiver>::type {
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    void start() noexcept {
      unifex::set_done(static_cast<Receiver&&>(receiver_));
    }
  };

  struct sender {
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    static constexpr bool sends_done = true;

    friend constexpr auto tag_invoke(
        tag_t<blocking>,
        const sender&) {
      return blocking_kind::always_inline;
    }

    template <typename Receiver>
    operation<Receiver> connect(Receiver&& receiver) const& {
      return operation<Receiver>{(Receiver &&) receiver};
    }
  };
} // namespace _ready_done

using ready_done_sender = _ready_done::sender;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
