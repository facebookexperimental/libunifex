#pragma once

#include <experimental/coroutine>
#include <type_traits>

namespace unifex {

namespace detail {
template <typename Awaitable, typename = void>
constexpr bool has_member_operator_co_await_v = false;

template <typename Awaitable>
constexpr bool has_member_operator_co_await_v<
    Awaitable,
    std::void_t<decltype(std::declval<Awaitable>().operator co_await())>> =
    true;

template <typename Awaitable, typename = void>
constexpr bool has_free_operator_co_await_v = false;

template <typename Awaitable>
constexpr bool has_free_operator_co_await_v<
    Awaitable,
    std::void_t<decltype(operator co_await(std::declval<Awaitable>()))>> = true;

template <typename Awaiter, typename = void>
struct await_result_impl {};

template <typename Awaiter>
struct await_result_impl<
    Awaiter,
    std::void_t<
        decltype(std::declval<Awaiter&>().await_ready() ? (void)0 : (void)0),
        decltype(std::declval<Awaiter&>().await_resume())>> {
  using type = decltype(std::declval<Awaiter&>().await_resume());
};

} // namespace detail

template <typename Awaitable>
decltype(auto) get_awaiter(Awaitable&& awaitable) noexcept {
  if constexpr (detail::has_member_operator_co_await_v<Awaitable>) {
    return static_cast<Awaitable&&>(awaitable).operator co_await();
  } else if constexpr (detail::has_free_operator_co_await_v<Awaitable>) {
    return operator co_await(static_cast<Awaitable&&>(awaitable));
  } else {
    return static_cast<Awaitable&&>(awaitable);
  }
}

template <typename Awaitable>
using awaiter_type_t = decltype(unifex::get_awaiter(std::declval<Awaitable>()));

template <typename Awaitable>
using await_result_t =
    typename detail::await_result_impl<awaiter_type_t<Awaitable>>::type;

} // namespace unifex
