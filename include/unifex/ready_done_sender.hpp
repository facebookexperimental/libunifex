#pragma once

#include <unifex/blocking.hpp>
#include <unifex/config.hpp>
#include <unifex/receiver_concepts.hpp>

#include <type_traits>

namespace unifex {

struct ready_done_sender {
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  friend constexpr blocking_kind tag_invoke(
      tag_t<cpo::blocking>,
      const ready_done_sender&) {
    return blocking_kind::always_inline;
  }

  template <typename Receiver>
  struct operation {
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    void start() noexcept {
      cpo::set_done(static_cast<Receiver&&>(receiver_));
    }
  };

  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) {
    return operation<std::remove_cvref_t<Receiver>>{(Receiver &&) receiver};
  }
};

} // namespace unifex
