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
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <type_traits>

namespace unifex {

struct inline_scheduler {
  struct schedule_task {
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    friend constexpr blocking_kind tag_invoke(
        tag_t<blocking>,
        const schedule_task&) noexcept {
      return blocking_kind::always_inline;
    }

    template <typename Receiver>
    struct operation {
      using stop_token_type = stop_token_type_t<Receiver&>;

      UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

      template <typename Receiver2>
      explicit operation(Receiver2&& r)
          : receiver_((Receiver2 &&) r) {}

      void start() noexcept {
        if constexpr (is_stop_never_possible_v<stop_token_type>) {
          cpo::set_value((Receiver &&) receiver_);
        } else {
          if (get_stop_token(receiver_).stop_requested()) {
            cpo::set_done((Receiver &&) receiver_);
          } else {
            cpo::set_value((Receiver &&) receiver_);
          }
        }
      }
    };

    template <typename Receiver>
    operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) {
      return operation<std::remove_cvref_t<Receiver>>{(Receiver &&) receiver};
    }
  };

  schedule_task schedule() {
    return {};
  }
};

} // namespace unifex
