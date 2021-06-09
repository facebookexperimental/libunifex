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
#include <unifex/coroutine.hpp>
#include <unifex/just_done.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/blocking.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _stop_if {
struct _fn {
private:
  struct _sender {
  private:
    template <typename Receiver>
    struct _op {
      struct type {
        Receiver rec_;
        void start() & noexcept {
          UNIFEX_TRY {
            if (get_stop_token(std::as_const(rec_)).stop_requested()) {
              unifex::set_done((Receiver&&) rec_);
            } else {
              unifex::set_value((Receiver&&) rec_);
            }
          } UNIFEX_CATCH (...) {
            unifex::set_error((Receiver&&) rec_, std::current_exception());
          }
        }
      };
    };

  public:
  #if !UNIFEX_NO_COROUTINES
    // Provide an awaiter interface in addition to the sender interface
    // because as an awaiter we can take advantage of symmetric transfer
    // to save stack space:
    bool await_ready() const noexcept {
      return false;
    }
    template <typename Promise>
    coro::coroutine_handle<> await_suspend(coro::coroutine_handle<Promise> coro) const noexcept {
      if (get_stop_token(coro.promise()).stop_requested()) {
        return coro.promise().unhandled_done();
      }
      return coro; // don't suspend
    }
    void await_resume() const noexcept {
    }
  #endif

    template<
      template<typename...> class Variant,
      template<typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template<
      template<typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template (typename Receiver)
      (requires receiver_of<Receiver>)
    auto connect(Receiver&& rec) const
      -> typename _op<remove_cvref_t<Receiver>>::type {
      return typename _op<remove_cvref_t<Receiver>>::type{(Receiver&&) rec};
    }

    friend constexpr auto tag_invoke(tag_t<unifex::blocking>, const _sender&) noexcept {
      return blocking_kind::always_inline;
    }
  };

public:
  [[nodiscard]] constexpr _sender operator()() const noexcept {
    return {};
  }
};
} // namespace _stop_if

namespace _stop {
struct _fn {
  [[nodiscard]] constexpr auto operator()() const noexcept {
    return just_done();
  }
};
} // namespace _stop

// Await this to cancel and unwind if stop has been requested:
inline constexpr _stop_if::_fn stop_if_requested {};

// Await this to cancel unconditionally:
inline constexpr _stop::_fn stop {};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
