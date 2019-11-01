#pragma once

#include <unifex/config.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/ready_done_sender.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/get_stop_token.hpp>

#include <type_traits>

namespace unifex {

struct never_sender {
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  template <typename Receiver>
  struct operation {
    struct cancel_callback {
      operation& op_;
      void operator()() noexcept {
        op_.stopCallback_.destruct();
        cpo::set_done(static_cast<Receiver&&>(op_.receiver_));
      }
    };

    using stop_token_type = stop_token_type_t<Receiver&>;

    static_assert(
        !is_stop_never_possible_v<stop_token_type>,
        "never should not be used with a stop-token "
        "type that can never be stopped.");

    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    manual_lifetime<
        typename stop_token_type::
        template callback_type<cancel_callback>>
      stopCallback_;

    template <typename Receiver2>
    operation(Receiver2&& receiver) : receiver_((Receiver2 &&) receiver) {}

    void start() noexcept {
      assert(get_stop_token(receiver_).stop_possible());
      stopCallback_.construct(
          get_stop_token(receiver_), cancel_callback{*this});
    }
  };

  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) {
    return operation<std::remove_cvref_t<Receiver>>{(Receiver &&) receiver};
  }
};

struct never_stream {
  never_sender next() noexcept {
    return {};
  }
  ready_done_sender cleanup() noexcept {
    return {};
  }
};

} // namespace unifex
