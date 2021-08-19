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
#include <unifex/blocking.hpp>
#include <unifex/std_concepts.hpp>

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _just_done {

template <typename Receiver>
struct _op {
  struct type;
};
template <typename Receiver>
using operation = typename _op<remove_cvref_t<Receiver>>::type;

template <typename Receiver>
struct _op<Receiver>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  void start() & noexcept {
    unifex::set_done((Receiver &&) receiver_);
  }
};

class sender {
 public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = true;

  template(typename This, typename Receiver)
      (requires same_as<remove_cvref_t<This>, sender> AND
        receiver<Receiver>)
  friend auto tag_invoke(tag_t<connect>, This&&, Receiver&& r)
      noexcept
      -> operation<Receiver> {
    return {static_cast<Receiver&&>(r)};
  }

  friend constexpr auto tag_invoke(tag_t<blocking>, const sender&) noexcept {
    return blocking_kind::always_inline;
  }
};
} // namespace _just_done

namespace _just_done_cpo {
  inline const struct just_done_fn {
    constexpr auto operator()() const noexcept
      -> _just_done::sender {
      return _just_done::sender{};
    }
  } just_done{};
} // namespace _just_done_cpo
using _just_done_cpo::just_done;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
