#pragma once

#include <unifex/config.hpp>
#include <unifex/receiver_concepts.hpp>

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

namespace unifex {

template <typename... Values>
class just_sender {
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;

 public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<Values...>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  template <typename... Values2>
  explicit just_sender(Values2&&... values) noexcept(
      noexcept((std::is_nothrow_constructible_v<Values, Values2> && ...)))
      : values_((Values2 &&) values...) {}

 private:
  template <typename Receiver>
  struct operation {
    UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    void start() noexcept {
      try {
        std::apply(
            [&](Values&&... values) {
              cpo::set_value((Receiver &&) receiver_, (Values &&) values...);
            },
            std::move(values_));
      } catch (...) {
        cpo::set_error((Receiver &&) receiver_, std::current_exception());
      }
    }
  };

 public:
  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& r) && {
    return {std::move(values_), (Receiver &&) r};
  }
};

template <typename... Values>
just_sender<std::decay_t<Values>...> just(Values&&... values) noexcept(
    (std::is_nothrow_constructible_v<std::decay_t<Values>, Values> && ...)) {
  return just_sender<std::decay_t<Values>...>{(Values &&) values...};
}

} // namespace unifex
