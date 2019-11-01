#pragma once

namespace unifex {

namespace detail {

template <typename CPO, typename Sig>
struct overloaded_cpo : CPO {
  constexpr overloaded_cpo() = default;
  constexpr overloaded_cpo(CPO) noexcept {}

  using type_erased_signature_t = Sig;
};

template <typename CPO>
struct base_cpo {
    using type = CPO;
};

template <typename CPO, typename Sig>
struct base_cpo<overloaded_cpo<CPO, Sig>> {
  using type = CPO;
};

template <typename CPO>
using base_cpo_t = typename base_cpo<CPO>::type;

template <typename CPO, typename Sig>
inline constexpr overloaded_cpo<CPO, Sig> overload_{};

} // namespace detail

template <typename Sig, typename CPO>
constexpr auto& overload(CPO) {
  return detail::overload_<CPO, Sig>;
}

} // namespace unifex
