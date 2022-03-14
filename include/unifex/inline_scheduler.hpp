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
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _inline_sched {
  template <typename Receiver>
  struct _op {
    struct type;
  };
  template <typename Receiver>
  using operation = typename _op<remove_cvref_t<Receiver>>::type;

  template <typename Receiver>
  struct _op<Receiver>::type final {
    using stop_token_type = stop_token_type_t<Receiver&>;

    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    template(typename Receiver2)
      (requires constructible_from<Receiver, Receiver2>)
    explicit type(Receiver2&& r) noexcept(std::is_nothrow_constructible_v<Receiver, Receiver2>)
      : receiver_((Receiver2 &&) r) {}

    void start() noexcept {
      UNIFEX_TRY {
        if constexpr (is_stop_never_possible_v<stop_token_type>) {
          unifex::set_value((Receiver &&) receiver_);
        } else {
          if (get_stop_token(receiver_).stop_requested()) {
            unifex::set_done((Receiver &&) receiver_);
          } else {
            unifex::set_value((Receiver &&) receiver_);
          }
        }
      } UNIFEX_CATCH (...) {
        unifex::set_error((Receiver &&) receiver_, std::current_exception());
      }
    }
  };

  struct scheduler {
    struct schedule_task {
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types = Variant<Tuple<>>;

      template <template <typename...> class Variant>
      using error_types = Variant<std::exception_ptr>;

      static constexpr bool sends_done = true;

      friend constexpr auto tag_invoke(
          tag_t<blocking>,
          const schedule_task&) noexcept {
        return blocking_kind::always_inline;
      }

      template <typename Receiver>
      operation<Receiver> connect(Receiver&& receiver) {
        return operation<Receiver>{(Receiver &&) receiver};
      }
    };

    constexpr schedule_task schedule() const noexcept {
      return {};
    }
    friend bool operator==(scheduler, scheduler) noexcept {
      return true;
    }
    friend bool operator!=(scheduler, scheduler) noexcept {
      return false;
    }
  };
} // namespace _inline_sched

using inline_scheduler = _inline_sched::scheduler;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
