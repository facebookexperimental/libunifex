#pragma once

#include <type_traits>

namespace unifex {

template <typename T, typename = void>
struct is_stop_never_possible : std::false_type {};

template <typename T>
struct is_stop_never_possible<
    T,
    std::enable_if_t<std::is_same_v<
        std::false_type,
        std::bool_constant<T{}.stop_possible()>>>> : std::true_type {};

template <typename T>
constexpr bool is_stop_never_possible_v = is_stop_never_possible<T>::value;

} // namespace unifex
