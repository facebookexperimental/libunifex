#pragma once

#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>

#include <atomic>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <variant>

namespace unifex {

namespace detail {

template <
    std::size_t Index,
    template <std::size_t> class Receiver,
    typename... Senders>
struct when_all_operation_tuple;

template <std::size_t Index, template <std::size_t> class Receiver>
struct when_all_operation_tuple<Index, Receiver> {
  template <typename Parent>
  explicit when_all_operation_tuple(Parent&) noexcept {}

  void start() noexcept {}
};

template <
    std::size_t Index,
    template <std::size_t> class Receiver,
    typename First,
    typename... Rest>
struct when_all_operation_tuple<Index, Receiver, First, Rest...>
    : when_all_operation_tuple<Index + 1, Receiver, Rest...> {
  template <typename Parent>
  explicit when_all_operation_tuple(
      Parent& parent,
      First&& first,
      Rest&&... rest)
      : when_all_operation_tuple<Index + 1, Receiver, Rest...>{parent,
                                                               (Rest &&)
                                                                   rest...},
        op_(cpo::connect((First &&) first, Receiver<Index>{parent})) {}

  void start() noexcept {
    cpo::start(op_);
    when_all_operation_tuple<Index + 1, Receiver, Rest...>::start();
  }

 private:
  operation_t<First, Receiver<Index>> op_;
};

} // namespace detail

template <typename... Senders>
class when_all_sender {
 public:
  static_assert(sizeof...(Senders) > 0);

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<
      typename Senders::template value_types<std::variant, std::tuple>...>>;

  template <template <typename...> class Variant>
  using error_types = deduplicate_t<concat_lists_t<
      Variant,
      Variant<std::exception_ptr>,
      typename Senders::template error_types<Variant>...>>;

  template <typename... Senders2>
  explicit when_all_sender(Senders2&&... senders)
      : senders_((Senders2 &&) senders...) {}

 private:
  template <typename Receiver>
  struct operation {
    struct cancel_operation {
      operation& op_;

      void operator()() noexcept {
        op_.stopSource_.request_stop();
      }
    };

    template <size_t Index>
    struct element_receiver {
      operation& op_;

      template <typename... Values>
      void value(Values&&... values) noexcept {
        try {
          std::get<Index>(op_.values_)
              .emplace(
                  std::in_place_type<std::tuple<Values...>>,
                  (Values &&) values...);
          op_.element_complete();
        } catch (...) {
          error(std::current_exception());
        }
      }

      template <typename Error>
      void error(Error&& error) noexcept {
        if (!op_.doneOrError_.exchange(true, std::memory_order_relaxed)) {
          op_.error_.emplace(std::in_place_type<Error>, (Error &&) error);
          op_.stopSource_.request_stop();
        }
        op_.element_complete();
      }

      void done() noexcept {
        if (!op_.doneOrError_.exchange(true, std::memory_order_relaxed)) {
          op_.stopSource_.request_stop();
        }
        op_.element_complete();
      }

      template <
          typename CPO,
          std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const element_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.op_.receiver_));
      }

      friend inplace_stop_token tag_invoke(
          tag_t<get_stop_token>,
          const element_receiver& r) noexcept {
        return r.op_.stopSource_.get_token();
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const element_receiver& r,
          Func&& func) {
        std::invoke(func, r.op_.receiver_);
      }
    };

    explicit operation(Receiver&& receiver, Senders&&... senders)
        : receiver_((Receiver &&) receiver),
          ops_(*this, (Senders &&) senders...) {}

    void start() noexcept {
      stopCallback_.construct(
          get_stop_token(receiver_), cancel_operation{*this});
      ops_.start();
    }

   private:
    void element_complete() noexcept {
      if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        deliver_result();
      }
    }

    void deliver_result() noexcept {
      stopCallback_.destruct();

      if (get_stop_token(receiver_).stop_requested()) {
        cpo::set_done(std::move(receiver_));
      } else if (doneOrError_.load(std::memory_order_relaxed)) {
        if (error_.has_value()) {
          std::visit(
              [this](auto&& error) {
                cpo::set_error(std::move(receiver_), (decltype(error))error);
              },
              std::move(error_.value()));
        } else {
          cpo::set_done(std::move(receiver_));
        }
      } else {
        deliver_value(std::index_sequence_for<Senders...>{});
      }
    }

    template <std::size_t... Indices>
    void deliver_value(std::index_sequence<Indices...>) noexcept {
      try {
        cpo::set_value(
            std::move(receiver_),
            std::get<Indices>(std::move(values_)).value()...);
      } catch (...) {
        cpo::set_error(std::move(receiver_), std::current_exception());
      }
    }

    std::tuple<std::optional<
        typename Senders::template value_types<std::variant, std::tuple>>...>
        values_;
    std::optional<error_types<std::variant>> error_;
    std::atomic<std::size_t> refCount_{sizeof...(Senders)};
    std::atomic<bool> doneOrError_{false};
    inplace_stop_source stopSource_;
    UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
        Receiver&>::template callback_type<cancel_operation>>
        stopCallback_;
    Receiver receiver_;
    detail::when_all_operation_tuple<0, element_receiver, Senders...> ops_;
  };

 public:
  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
    return std::apply([&](Senders&&... senders) {
      return operation<std::remove_cvref_t<Receiver>>{(Receiver &&) receiver,
                                                      (Senders &&) senders...};
    }, std::move(senders_));
  }

 private:
  std::tuple<Senders...> senders_;
};

template <typename... Senders>
when_all_sender<std::remove_cvref_t<Senders>...> when_all(
    Senders&&... senders) {
  return when_all_sender<std::remove_cvref_t<Senders>...>{(Senders &&)
                                                              senders...};
}

} // namespace unifex
