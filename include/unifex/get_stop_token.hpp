#pragma once

#include <unifex/stop_token_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/unstoppable_token.hpp>

namespace unifex {

inline constexpr struct get_stop_token_cpo {
  template <typename T>
  auto operator()([[maybe_unused]] const T& value) const noexcept ->
      typename std::conditional_t<
          is_tag_invocable_v<get_stop_token_cpo, const T&>,
          tag_invoke_result<get_stop_token_cpo, const T&>,
          identity<unstoppable_token>>::type {
    if constexpr (is_tag_invocable_v<get_stop_token_cpo, const T&>) {
      static_assert(
          is_nothrow_tag_invocable_v<get_stop_token_cpo, const T&>,
          "get_stop_token() customisations must be declared noexcept");
      return tag_invoke(get_stop_token_cpo{}, value);
    } else {
      return unstoppable_token{};
    }
  }
} get_stop_token{};

template <typename T>
using get_stop_token_result_t =
    std::invoke_result_t<decltype(get_stop_token), T>;

template <typename Receiver>
using stop_token_type_t =
    std::remove_cvref_t<get_stop_token_result_t<Receiver>>;

} // namespace unifex
