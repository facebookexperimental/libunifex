#pragma once

#include <unifex/tag_invoke.hpp>
#include <unifex/sender_concepts.hpp>

namespace unifex {
namespace cpo {

inline constexpr struct next_cpo {
  template <typename Stream>
  friend auto tag_invoke(next_cpo, Stream& s) noexcept(noexcept(s.next()))
      -> decltype(s.next()) {
    return s.next();
  }

  template <typename Stream>
  constexpr auto operator()(Stream& stream) const
      noexcept(is_nothrow_tag_invocable_v<next_cpo, Stream&>)
          -> tag_invoke_result_t<next_cpo, Stream&> {
    return tag_invoke(*this, stream);
  }
} next{};

inline constexpr struct cleanup_cpo {
  template <typename Stream>
  friend auto tag_invoke(cleanup_cpo, Stream& s) noexcept(noexcept(s.cleanup()))
      -> decltype(s.cleanup()) {
    return s.cleanup();
  }

  template <typename Stream>
  constexpr auto operator()(Stream& stream) const
      noexcept(is_nothrow_tag_invocable_v<cleanup_cpo, Stream&>)
          -> tag_invoke_result_t<cleanup_cpo, Stream&> {
    return tag_invoke(*this, stream);
  }
} cleanup{};

} // namespace cpo

template <typename Stream>
using next_sender_t = decltype(cpo::next(std::declval<Stream&>()));

template <typename Stream>
using cleanup_sender_t = decltype(cpo::cleanup(std::declval<Stream&>()));

template <typename Stream, typename Receiver>
using next_operation_t = operation_t<next_sender_t<Stream>, Receiver>;

template <typename Stream, typename Receiver>
using cleanup_operation_t = operation_t<cleanup_sender_t<Stream>, Receiver>;

} // namespace unifex
