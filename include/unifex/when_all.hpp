/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/async_trace.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/blocking.hpp>

#include <atomic>
#include <cstddef>
#include <optional>
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
        op_(connect((First &&) first, Receiver<Index>{parent})) {}

  void start() noexcept {
    unifex::start(op_);
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
  using error_types = typename concat_type_lists_unique_t<
      typename Senders::template error_types<type_list>...,
      type_list<std::exception_ptr>>::template apply<Variant>;

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
      void set_value(Values&&... values) noexcept {
        try {
          std::get<Index>(op_.values_)
              .emplace(
                  std::in_place_type<std::tuple<Values...>>,
                  (Values &&) values...);
          op_.element_complete();
        } catch (...) {
          this->set_error(std::current_exception());
        }
      }

      template <typename Error>
      void set_error(Error&& error) noexcept {
        if (!op_.doneOrError_.exchange(true, std::memory_order_relaxed)) {
          op_.error_.emplace(std::in_place_type<Error>, (Error &&) error);
          op_.stopSource_.request_stop();
        }
        op_.element_complete();
      }

      void set_done() noexcept {
        if (!op_.doneOrError_.exchange(true, std::memory_order_relaxed)) {
          op_.stopSource_.request_stop();
        }
        op_.element_complete();
      }

      Receiver& get_receiver() const { return op_.receiver_; }

      template <
          typename CPO,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const element_receiver& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(std::as_const(r.get_receiver()));
      }

      inplace_stop_source& get_stop_source() const {
          return op_.stopSource_;
      }

      friend inplace_stop_token tag_invoke(
          tag_t<get_stop_token>,
          const element_receiver& r) noexcept {
        return r.get_stop_source().get_token();
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const element_receiver& r,
          Func&& func) {
        std::invoke(func, r.get_receiver());
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
        unifex::set_done(std::move(receiver_));
      } else if (doneOrError_.load(std::memory_order_relaxed)) {
        if (error_.has_value()) {
          std::visit(
              [this](auto&& error) {
                unifex::set_error(std::move(receiver_), (decltype(error))error);
              },
              std::move(error_.value()));
        } else {
          unifex::set_done(std::move(receiver_));
        }
      } else {
        deliver_value(std::index_sequence_for<Senders...>{});
      }
    }

    template <std::size_t... Indices>
    void deliver_value(std::index_sequence<Indices...>) noexcept {
      try {
        unifex::set_value(
            std::move(receiver_),
            std::get<Indices>(std::move(values_)).value()...);
      } catch (...) {
        unifex::set_error(std::move(receiver_), std::current_exception());
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

  // Customise the 'blocking' CPO to combine the blocking-nature
  // of each of the child operations.
  friend blocking_kind tag_invoke(tag_t<blocking>, const when_all_sender& s) noexcept {
    bool alwaysInline = true;
    bool alwaysBlocking = true;
    bool neverBlocking =  false;

    auto handleBlockingStatus = [&](blocking_kind kind) noexcept {
      switch (kind) {
        case blocking_kind::never:
          neverBlocking = true;
          [[fallthrough]];
        case blocking_kind::maybe:
          alwaysBlocking = false;
          [[fallthrough]];
        case blocking_kind::always:
          alwaysInline = false;
          [[fallthrough]];
        case blocking_kind::always_inline:
          break;
      }
    };

    std::apply([&](const auto&... senders) {
      (void)std::initializer_list<int>{
        (handleBlockingStatus(blocking(senders)), 0)... };
    }, s.senders_);

    if (neverBlocking) {
      return blocking_kind::never;
    } else if (alwaysInline) {
      return blocking_kind::always_inline;
    } else if (alwaysBlocking) {
      return blocking_kind::always;
    } else {
      return blocking_kind::maybe;
    }
  }

  std::tuple<Senders...> senders_;
};

template <typename... Senders>
when_all_sender<std::remove_cvref_t<Senders>...> when_all(
    Senders&&... senders) {
  return when_all_sender<std::remove_cvref_t<Senders>...>{(Senders &&)
                                                              senders...};
}

} // namespace unifex
