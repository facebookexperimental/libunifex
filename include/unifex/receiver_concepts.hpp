#pragma once

#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>

#include <type_traits>

namespace unifex {

namespace cpo {

inline constexpr struct set_value_cpo {
  template <typename Receiver, typename... Values>
  friend auto
  tag_invoke(set_value_cpo, Receiver&& r, Values&&... values) noexcept(
      noexcept(static_cast<Receiver&&>(r).value((Values &&) values...)))
      -> decltype(static_cast<Receiver&&>(r).value((Values &&) values...)) {
    return static_cast<Receiver&&>(r).value((Values &&) values...);
  }

  template <typename Receiver, typename... Values>
  auto operator()(Receiver&& r, Values&&... values) const noexcept(
      noexcept(tag_invoke(*this, (Receiver &&) r, (Values &&) values...)))
      -> requires_t<
          std::is_void,
          tag_invoke_result_t<set_value_cpo, Receiver, Values...>> {
    return tag_invoke(*this, (Receiver &&) r, (Values &&) values...);
  }
} set_value{};

inline constexpr struct set_error_cpo {
  template <typename Receiver, typename Error>
  friend auto tag_invoke(set_error_cpo, Receiver&& r, Error&& e) noexcept
      -> decltype(static_cast<Receiver&&>(r).error((Error &&) e)) {
    static_assert(
        noexcept(static_cast<Receiver&&>(r).error((Error &&) e)),
        "receiver.error() method must be nothrow invocable");
    return static_cast<Receiver&&>(r).error((Error &&) e);
  }

  template <typename Receiver, typename Error>
  auto operator()(Receiver&& r, Error&& error) const noexcept -> requires_t<
      std::is_void,
      tag_invoke_result_t<set_error_cpo, Receiver, Error>> {
    static_assert(
        noexcept(tag_invoke(*this, (Receiver &&) r, (Error &&) error)),
        "set_error() invocation is required to be noexcept.");
    return tag_invoke(*this, (Receiver &&) r, (Error &&) error);
  }
} set_error{};

inline constexpr struct set_done_cpo {
  template <typename Receiver>
  friend auto tag_invoke(set_done_cpo, Receiver&& r) noexcept
      -> decltype(static_cast<Receiver&&>(r).done()) {
    static_assert(
        noexcept(static_cast<Receiver&&>(r).done()),
        "receiver.done() method must be nothrow invocable");
    return static_cast<Receiver&&>(r).done();
  }

  template <typename Receiver>
  auto operator()(Receiver&& r) const noexcept
      -> requires_t<std::is_void, tag_invoke_result_t<set_done_cpo, Receiver>> {
    static_assert(
        noexcept(tag_invoke(*this, (Receiver &&) r)),
        "set_done() invocation is required to be noexcept.");
    return tag_invoke(*this, (Receiver &&) r);
  }
} set_done{};

template <typename T>
constexpr bool is_receiver_cpo_v = is_one_of_v<
    std::remove_cvref_t<T>,
    set_value_cpo,
    set_error_cpo,
    set_done_cpo>;

} // namespace cpo
} // namespace unifex
