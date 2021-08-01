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
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _trampoline {
class scheduler {
  std::size_t maxRecursionDepth_;

 public:
  scheduler() noexcept : maxRecursionDepth_(16) {}

  explicit scheduler(std::size_t depth) noexcept
      : maxRecursionDepth_(depth) {}

 private:
  struct operation_base {
    using execute_fn = void(operation_base*) noexcept;

    explicit operation_base(execute_fn* execute, std::size_t maxDepth) noexcept
    : execute_(execute)
    , maxRecursionDepth_(maxDepth)
    {}

    void execute() noexcept {
      execute_(this);
    }

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

    operation_base* next_ = nullptr;
    execute_fn* execute_;
    std::size_t maxRecursionDepth_;
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
  struct _op {
    class type final : operation_base {
      UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

      friend schedule_sender;

      template <typename Receiver2>
      explicit type(Receiver2&& receiver, std::size_t maxDepth)
        : operation_base(&type::execute_impl, maxDepth)
        , receiver_((Receiver2 &&) receiver) {}

      static void execute_impl(operation_base* p) noexcept {
        auto& self = *static_cast<type*>(p);
        if (is_stop_never_possible_v<stop_token_type_t<Receiver&>>) {
          unifex::set_value(static_cast<Receiver&&>(self.receiver_));
        } else {
          if (get_stop_token(self.receiver_).stop_requested()) {
            unifex::set_done(static_cast<Receiver&&>(self.receiver_));
          } else {
            unifex::set_value(static_cast<Receiver&&>(self.receiver_));
          }
        }
      }
      
    public:
      using operation_base::start;
    };
  };
  template <typename Receiver>
  using operation = typename _op<remove_cvref_t<Receiver>>::type;

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

    static constexpr bool sends_done = true;

    template <typename Receiver>
    operation<Receiver> connect(Receiver&& receiver) const& {
      return operation<Receiver>{(Receiver &&) receiver, maxRecursionDepth_};
    }

  private:
    std::size_t maxRecursionDepth_;
  };

public:
  schedule_sender schedule() const noexcept {
    return schedule_sender{maxRecursionDepth_};
  }
  friend bool operator==(scheduler, scheduler) noexcept {
    return true;
  }
  friend bool operator!=(scheduler, scheduler) noexcept {
    return false;
  }
};
} // namespace _trampoline

using trampoline_scheduler = _trampoline::scheduler;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
