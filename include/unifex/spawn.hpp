#pragma once

#include <unifex/blocking.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>

namespace unifex {

template <typename Sender, typename Receiver>
struct spawned_op {
  struct wrapped_receiver {
    spawned_op* op_;

    explicit wrapped_receiver(spawned_op* op) noexcept : op_(op) {}

    template <typename... Values>
        void value(Values&&... values) && noexcept {
      cpo::set_value(std::move(op_->receiver_), (Values &&) values...);
      delete op_;
    }

    template <typename Error>
    void error(Error&& error) && noexcept {
      cpo::set_error(std::move(op_->receiver_), (Error &&) error);
      delete op_;
    }

    void done() && noexcept {
      cpo::set_done(std::move(op_->receiver_));
      delete op_;
    }

    template <
        typename CPO,
        std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
    friend auto tag_invoke(CPO cpo, const wrapped_receiver& r) noexcept(
        std::is_nothrow_invocable_v<CPO, const Receiver&>)
        -> std::invoke_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.op_->receiver_));
    }

    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const wrapped_receiver& r,
        Func&& func) {
      std::invoke(func, r.op_->receiver_);
    }
  };

  template <typename Receiver2>
  spawned_op(Sender&& sender, Receiver2&& receiver)
      : receiver_((Receiver2 &&) receiver),
        inner_(cpo::connect((Sender &&) sender, wrapped_receiver{this}))
      {}

  void start() & noexcept {
    cpo::start(inner_);
  }

  Receiver receiver_;
  operation_t<Sender, wrapped_receiver> inner_;
};

template <typename Sender, typename Receiver>
void spawn(Sender&& sender, Receiver&& receiver) noexcept {
  auto blocking = cpo::blocking(sender);
  if (blocking == blocking_kind::always ||
      blocking == blocking_kind::always_inline) {
    // The sender will complete synchronously so we can avoid allocating the
    // state on the heap.
    auto op = cpo::connect((Sender &&) sender, (Receiver &&) receiver);
    cpo::start(op);
  } else {
    auto* op = new spawned_op<Sender, std::remove_cvref_t<Receiver>>(
        (Sender &&) sender, (Receiver &&) receiver);
    op->start();
  }
}

} // namespace unifex
