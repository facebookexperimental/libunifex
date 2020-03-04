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
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace unifex {

class trampoline_scheduler {
  std::size_t maxRecursionDepth_;

 public:
  trampoline_scheduler() noexcept : maxRecursionDepth_(16) {}

  explicit trampoline_scheduler(std::size_t depth) noexcept
      : maxRecursionDepth_(depth) {}

private:
  struct operation_base {
    operation_base* next_ = nullptr;
    virtual void execute() noexcept = 0;
  };

  struct trampoline_state {
    static thread_local trampoline_state* current_;

    trampoline_state() noexcept {
      current_ = this;
    }

    ~trampoline_state() {
      current_ = nullptr;
    }

    void drain() noexcept;

    std::size_t recursionDepth_ = 1;
    operation_base* head_ = nullptr;
  };

  class schedule_sender;

  template <typename Receiver>
  class operation final : operation_base {
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    std::size_t maxRecursionDepth_;

    friend schedule_sender;

    template <typename Receiver2>
    explicit operation(Receiver2&& receiver, std::size_t maxDepth)
        : receiver_((Receiver2 &&) receiver), maxRecursionDepth_(maxDepth) {}

    void execute() noexcept final {
      if (is_stop_never_possible_v<stop_token_type_t<Receiver&>>) {
        unifex::set_value(static_cast<Receiver&&>(receiver_));
      } else {
        if (get_stop_token(receiver_).stop_requested()) {
          unifex::set_done(static_cast<Receiver&&>(receiver_));
        } else {
          unifex::set_value(static_cast<Receiver&&>(receiver_));
        }
      }
    }
  public:
    void start() noexcept {
      auto* currentState = trampoline_state::current_;
      if (currentState == nullptr) {
        trampoline_state state;
        execute();
        state.drain();
      } else if (currentState->recursionDepth_ < maxRecursionDepth_) {
        ++currentState->recursionDepth_;
        execute();
      } else {
        // Exceeded recursion limit.
        next_ = std::exchange(
          currentState->head_,
          static_cast<operation_base*>(this));
      }
    }
  };

  class schedule_sender {
  public:
    explicit schedule_sender(std::size_t maxDepth) noexcept
    : maxRecursionDepth_(maxDepth)
    {}

    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    template <typename Receiver>
    operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
      return operation<std::remove_cvref_t<Receiver>>{(Receiver &&) receiver,
                                                      maxRecursionDepth_};
    }

  private:
    std::size_t maxRecursionDepth_;
  };

public:
  schedule_sender schedule() const noexcept {
    return schedule_sender{maxRecursionDepth_};
  }
};

} // namespace unifex
