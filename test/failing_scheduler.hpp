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
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>

#include <exception>
#include <stdexcept>

namespace unifex_test {

using namespace unifex;

enum class schedule_failure { error, done };

// schedule() completes inline with set_error() or set_done(), regardless of
// the receiver's stop token.
template <schedule_failure Failure>
struct failing_scheduler {
  template <typename Receiver>
  struct operation {
    void start() noexcept {
      if constexpr (Failure == schedule_failure::error) {
#if UNIFEX_NO_EXCEPTIONS
        // libc++'s make_exception_ptr() aborts without exceptions
        unifex::set_error(std::move(receiver_), std::exception_ptr{});
#else
        unifex::set_error(
            std::move(receiver_),
            std::make_exception_ptr(std::runtime_error("schedule failed")));
#endif
      } else {
        unifex::set_done(std::move(receiver_));
      }
    }

    Receiver receiver_;
  };

  struct sender {
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;
    static constexpr blocking_kind blocking = blocking_kind::always_inline;

    template <typename Receiver>
    operation<remove_cvref_t<Receiver>> connect(Receiver&& r) const {
      return {std::forward<Receiver>(r)};
    }
  };

  sender schedule() const noexcept { return {}; }

  friend bool operator==(failing_scheduler, failing_scheduler) noexcept {
    return true;
  }
  friend bool operator!=(failing_scheduler, failing_scheduler) noexcept {
    return false;
  }
};

}  // namespace unifex_test
