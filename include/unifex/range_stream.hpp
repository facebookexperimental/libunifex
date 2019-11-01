#pragma once

#include <unifex/blocking.hpp>
#include <unifex/ready_done_sender.hpp>
#include <unifex/receiver_concepts.hpp>

#include <type_traits>
#include <utility>

namespace unifex {

struct range_stream {
  int next_;
  int max_;

  explicit range_stream(int max) : next_(0), max_(max) {}
  explicit range_stream(int start, int max) : next_(start), max_(max) {}

  struct next_sender {
    range_stream& stream_;

    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<int>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    friend constexpr blocking_kind tag_invoke(
        tag_t<cpo::blocking>,
        const range_stream&) noexcept {
      return blocking_kind::always_inline;
    }

    template <typename Receiver>
    struct operation {
      range_stream& stream_;
      Receiver receiver_;

      void start() noexcept {
        if (stream_.next_ < stream_.max_) {
          cpo::set_value(std::move(receiver_), stream_.next_++);
        } else {
          cpo::set_done(std::move(receiver_));
        }
      }
    };

    template <typename Receiver>
    operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
      return operation<std::remove_cvref_t<Receiver>>{stream_,
                                                      (Receiver &&) receiver};
    }
  };

  next_sender next() & {
    return next_sender{*this};
  }

  ready_done_sender cleanup() & {
    return {};
  }
};

} // namespace unifex
