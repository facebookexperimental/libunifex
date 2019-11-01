#pragma once

#include <unifex/config.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/manual_lifetime_union.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>

#include <exception>
#include <functional>
#include <tuple>
#include <utility>
#include <type_traits>

namespace unifex {

template <typename Predecessor, typename Successor>
struct typed_via_sender {
  Predecessor pred_;
  Successor succ_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = typename Predecessor::
      template value_types<Variant, decayed_tuple<Tuple>::template apply>;

  template <template <typename...> class Variant>
  using error_types = concat_unique_t<
      typename Predecessor::template error_types<
          decayed_tuple<Variant>::template apply>,
      typename Successor::template error_types<decayed_tuple<
          append_unique<Variant, std::exception_ptr>::template apply>::
                                                   template apply>>;

  template <typename Receiver>
  struct operation {
    template <typename... Values>
    struct value_receiver {
      operation& op_;

      void value() noexcept {
        auto& op = op_;
        auto& valueOp =
            op.succValueOp_.template get<value_operation<Values...>>();

        auto& storedValue = op.value_.template get<std::tuple<Values...>>();

        valueOp.destruct();
        try {
            auto values = std::invoke([&]() {
              scope_guard g{[&]() noexcept { storedValue.destruct(); }};
              return std::tuple<Values...>(std::move(storedValue).get());
            });
            std::apply(
              [&](Values&&... values) noexcept {
                cpo::set_value(
                  std::forward<Receiver>(op.receiver_),
                  (Values &&) values...);
              },
              std::move(values));
        } catch (...) {
          cpo::set_error(
              std::forward<Receiver>(op.receiver_),
              std::current_exception());
        }
      }

      template <typename Error>
      void error(Error&& error) noexcept {
        auto& op = op_;
        auto& valueOp = op.succValueOp_.template get<value_operation<Values...>>();
        auto& storedValue = op.value_.template get<std::tuple<Values...>>();
        valueOp.destruct();
        storedValue.destruct();
        cpo::set_error(
            std::forward<Receiver>(op.receiver_), (Error &&) error);
      }

      void done() noexcept {
        auto& op = op_;
        auto& valueOp = op.succValueOp_.template get<value_operation<Values...>>();
        auto& storedValue = op.value_.template get<std::tuple<Values...>>();
        valueOp.destruct();
        storedValue.destruct();
        cpo::set_done(std::forward<Receiver>(op.receiver_));
      }

      template <
          typename CPO,
          std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const value_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.op_.receiver_));
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const value_receiver& r,
          Func&& func) {
        std::invoke(func, r.op_.receiver_);
      }
    };

    template <typename Error>
    struct error_receiver {
      operation& op_;

      void value() noexcept {
        auto& op = op_;
        auto& errorOp = op.succErrorOp_.template get<error_operation<Error>>();
        errorOp.destruct();

        auto& storedError = op.error_.template get<Error>();
        auto error = std::move(storedError).get();
        storedError.destruct();

        cpo::set_error(
            std::forward<Receiver>(op.receiver_), std::move(error));
      }

      template <typename OtherError>
      void error(OtherError&& otherError) noexcept {
        auto& op = op_;

        auto& errorOp = op.succErrorOp_.template get<error_operation<Error>>();
        errorOp.destruct();

        auto& storedError = op.error_.template get<Error>();
        storedError.destruct();

        cpo::set_error(
            std::forward<Receiver>(op.receiver_), (OtherError &&) otherError);
      }

      void done() noexcept {
        auto& op = op_;
        auto& errorOp = op.succErrorOp_.template get<error_operation<Error>>();
        auto& storedError = op.error_.template get<Error>();
        errorOp.destruct();
        storedError.destruct();
        cpo::set_done(std::forward<Receiver>(op.receiver_));
      }

      template <
          typename CPO,
          std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const error_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.op_.receiver_));
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const error_receiver& r,
          Func&& func) {
        std::invoke(func, r.op_.receiver_);
      }
    };

    struct done_receiver {
      operation& op_;

      void value() noexcept {
        auto& op = op_;
        op.succDoneOp_.destruct();
        cpo::set_done(std::forward<Receiver>(op.receiver_));
      }

      template <typename OtherError>
      void error(OtherError&& otherError) noexcept {
        auto& op = op_;
        op.succDoneOp_.destruct();
        cpo::set_error(
            std::forward<Receiver>(op.receiver_), (OtherError &&) otherError);
      }

      void done() noexcept {
        auto& op = op_;
        op.succDoneOp_.destruct();
        cpo::set_done(std::forward<Receiver>(op.receiver_));
      }

      template <
          typename CPO,
          std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const done_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.op_.receiver_));
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const done_receiver& r,
          Func&& func) {
        std::invoke(func, r.op_.receiver_);
      }
    };

    struct predecessor_receiver {
      operation& op_;

      template <typename... Values>
      void value(Values&&... values) && noexcept {
        auto& op = op_;

        auto& storedValue =
            op.value_.template get<std::tuple<std::remove_cvref_t<Values>...>>();

        try {
          storedValue.construct((Values &&) values...);
        } catch (...) {
          std::move(*this).error(std::current_exception());
          return;
        }

        op.predOp_.destruct();

        try {
          cpo::start(
              op.succValueOp_
                  .template get<value_operation<std::remove_cvref_t<Values>...>>()
                  .construct_from([&] {
                    return cpo::connect(
                        static_cast<Successor&&>(op.succ_),
                        value_receiver<std::remove_cvref_t<Values>...>{op});
                  }));
        } catch (...) {
          storedValue.destruct();
          cpo::set_error(
              std::forward<Receiver>(op.receiver_),
              std::current_exception());
        }
      }

      template <typename Error>
      void error(Error&& e) && noexcept {
        auto& op = op_;

        static_assert(
            std::is_nothrow_constructible_v<std::remove_cvref_t<Error>, Error>,
            "Error types must be nothrow copyable/movable");

        auto& storedError =
            op.error_.template get<std::remove_cvref_t<Error>>();
        storedError.construct((Error &&) e);

        op.predOp_.destruct();
        try {
          cpo::start(
              op.succErrorOp_
                  .template get<error_operation<std::remove_cvref_t<Error>>>()
                  .construct_from([&] {
                    return cpo::connect(
                        static_cast<Successor&&>(op.succ_),
                        error_receiver<std::remove_cvref_t<Error>>{op});
                  }));
        } catch (...) {
          storedError.destruct();
          cpo::set_error(
              std::forward<Receiver>(op.receiver_),
              std::current_exception());
        }
      }

      void done() && noexcept {
        auto& op = op_;
        try {
          cpo::start(op.succDoneOp_.construct_from([&] {
            return cpo::connect(
                static_cast<Successor&&>(op.succ_),
                done_receiver{op});
          }));
        } catch (...) {
          cpo::set_error(
              std::forward<Receiver>(op.receiver_),
              std::current_exception());
        }
      }

      template <
          typename CPO,
          std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const predecessor_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.op_.receiver_));
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const predecessor_receiver& r,
          Func&& func) {
        std::invoke(func, r.op_.receiver_);
      }
    };

    UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
    UNIFEX_NO_UNIQUE_ADDRESS Successor succ_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    template <typename... Values>
    using value_operation =
        operation_t<Successor, value_receiver<Values...>>;

    template <typename Error>
    using error_operation =
        operation_t<Successor, error_receiver<Error>>;

    template <typename... Errors>
    using error_union = manual_lifetime_union<error_operation<Errors>...>;

    union {
      error_types<manual_lifetime_union> error_;
      value_types<manual_lifetime_union, std::tuple> value_;
    };

    union {
      manual_lifetime<operation_t<Predecessor, predecessor_receiver>>
          predOp_;
      manual_lifetime<operation_t<Successor, done_receiver>>
          succDoneOp_;
      value_types<manual_lifetime_union, value_operation> succValueOp_;
      error_types<error_union> succErrorOp_;
    };

    template <typename Receiver2>
    explicit operation(
        Predecessor&& pred,
        Successor&& succ,
        Receiver2&& receiver)
        : pred_((Predecessor&&) pred),
          succ_((Successor&&) succ),
          receiver_((Receiver2&&) receiver)
    {}

    ~operation() {}

    void start() noexcept {
      try {
        predOp_.construct_from([&] {
          return cpo::connect(
              static_cast<Predecessor&&>(pred_),
              predecessor_receiver{*this});
        });
        cpo::start(predOp_.get());
      } catch (...) {
        cpo::set_error(std::move(receiver_), std::current_exception());
      }
    }
  };

  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
    return operation<std::remove_cvref_t<Receiver>>{
       (Predecessor &&) pred_,
       (Successor &&) succ_,
       (Receiver &&) receiver};
  }
};

template <typename Predecessor, typename Successor>
typed_via_sender<
    std::remove_cvref_t<Predecessor>,
    std::remove_cvref_t<Successor>>
typed_via(Successor&& succ, Predecessor&& pred) {
  return typed_via_sender<
      std::remove_cvref_t<Predecessor>,
      std::remove_cvref_t<Successor>>{(Predecessor &&) pred,
                                      (Successor &&) succ};
}

} // namespace unifex
