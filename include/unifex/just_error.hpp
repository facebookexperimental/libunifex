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
namespace _just_error {

template <typename Receiver, typename Error>
struct _op {
  struct type;
};
template <typename Receiver, typename Error>
using operation = typename _op<remove_cvref_t<Receiver>, Error>::type;

template <typename Receiver, typename Error>
struct _op<Receiver, Error>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Error error_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  void start() & noexcept {
    unifex::set_error((Receiver &&) receiver_, (Error &&) error_);
  }
};

template <typename Error>
struct _sender {
  class type;
};
template <typename Error>
using sender = typename _sender<std::decay_t<Error>>::type;

template <typename Error>
class _sender<Error>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Error error_;

  public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<>;

  template <template <typename...> class Variant>
  using error_types = Variant<Error>;

  static constexpr bool sends_done = false;

  template<typename Error2>
  explicit type(std::in_place_t, Error2&& error)
    noexcept(std::is_nothrow_constructible_v<Error, Error2>)
    : error_((Error2 &&) error) {}

  template(typename This, typename Receiver)
      (requires same_as<remove_cvref_t<This>, type> AND
        receiver<Receiver, Error> AND
        constructible_from<Error, member_t<This, Error>>)
  friend auto tag_invoke(tag_t<connect>, This&& that, Receiver&& r)
      noexcept(std::is_nothrow_constructible_v<Error, member_t<This, Error>>)
      -> operation<Receiver, Error> {
    return {static_cast<This&&>(that).error_, static_cast<Receiver&&>(r)};
  }

  friend constexpr auto tag_invoke(tag_t<blocking>, const type&) noexcept {
    return blocking_kind::always_inline;
  }
};
} // namespace _just_error

namespace _just_error_cpo {
  inline const struct just_error_fn {
    template <typename Error>
    constexpr auto operator()(Error&& error) const
      noexcept(std::is_nothrow_constructible_v<std::decay_t<Error>, Error>)
      -> _just_error::sender<Error> {
      return _just_error::sender<Error>{std::in_place, (Error&&) error};
    }
  } just_error{};
} // namespace _just_error_cpo
using _just_error_cpo::just_error;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
