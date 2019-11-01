#pragma once

#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/ready_done_sender.hpp>
#include <unifex/scope_guard.hpp>

#include <optional>
#include <type_traits>

namespace unifex {

template <typename Sender>
struct single_stream {
  std::optional<Sender> sender_;

  struct next_sender {
    std::optional<Sender> sender_;

    template<template<typename...> class Variant,
             template<typename...> class Tuple>
    using value_types = typename Sender::template value_types<Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types = typename Sender::template error_types<Variant>;

    template <typename Receiver>
    struct operation {
      union {
        Receiver receiver_;
        manual_lifetime<operation_t<Sender, Receiver>> innerOp_;
      };
      bool done_;

      template <typename Receiver2>
      explicit operation(Receiver2&& receiver)
      : receiver_((Receiver2&&)receiver)
      , done_(true)
      {}

      explicit operation(Sender&& sender, Receiver&& receiver)
      : done_(false)
      {
        innerOp_.construct_from([&] {
          return cpo::connect(
              static_cast<Sender&&>(sender), (Receiver&&)receiver);
        });
      }

      ~operation() {
        if (done_) {
          receiver_.~Receiver();
        } else {
          innerOp_.destruct();
        }
      }

      void start() noexcept {
        if (done_) {
          cpo::set_done(std::move(receiver_));
        } else {
          cpo::start(innerOp_.get());
        }
      }
    };

    template <typename Receiver>
    auto connect(Receiver&& receiver) {
      if (sender_) {
        return operation<Receiver>{*std::move(sender_), (Receiver&&)receiver};
      } else {
        return operation<Receiver>{(Receiver&&)receiver};
      }
    }
  };

  next_sender next() {
    scope_guard g{[&]() noexcept { sender_.reset(); }};
    return next_sender{std::move(sender_)};
  }

  ready_done_sender cleanup() noexcept {
    return {};
  }

  template <typename Sender2>
  explicit single_stream(Sender2&& sender)
  : sender_(std::in_place, (Sender2&&)sender) {}
};

template <typename Sender>
auto single(Sender&& sender) {
  return single_stream<std::remove_cvref_t<Sender>>{(Sender&&)sender};
}

} // namespace unifex
