#pragma once

#include <unifex/config.hpp>

#include <type_traits>

namespace unifex {
namespace tag_invoke_impl {

void tag_invoke() = delete;

struct tag_invoke_cpo {
  template <typename CPO, typename... Args>
  constexpr auto operator()(CPO cpo, Args&&... args) const
      noexcept(noexcept(tag_invoke((CPO&&)cpo, (Args &&) args...)))
          -> decltype(tag_invoke((CPO&&)cpo, (Args &&) args...)) {
    return tag_invoke((CPO&&)cpo, (Args &&) args...);
  }
};
} // namespace tag_invoke_impl

namespace tag_invoke_cpo_ns {
inline constexpr tag_invoke_impl::tag_invoke_cpo tag_invoke{};
}
using namespace tag_invoke_cpo_ns;

template <auto& CPO>
using tag_t = std::remove_cvref_t<decltype(CPO)>;

template <typename CPO, typename... Args>
using tag_invoke_result = std::invoke_result<tag_t<tag_invoke>, CPO, Args...>;

template <typename CPO, typename... Args>
using tag_invoke_result_t =
    std::invoke_result_t<tag_t<tag_invoke>, CPO, Args...>;

template <typename CPO, typename... Args>
inline constexpr bool is_tag_invocable_v =
    std::is_invocable_v<tag_t<tag_invoke>, CPO, Args...>;

template <typename CPO, typename... Args>
using is_tag_invocable = std::is_invocable<tag_t<tag_invoke>, CPO, Args...>;

template <typename CPO, typename... Args>
inline constexpr bool is_nothrow_tag_invocable_v =
    std::is_nothrow_invocable_v<tag_t<tag_invoke>, CPO, Args...>;

template <typename CPO, typename... Args>
using is_nothrow_tag_invocable =
    std::is_nothrow_invocable<tag_t<tag_invoke>, CPO, Args...>;

} // namespace unifex
