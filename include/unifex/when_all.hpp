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
#include <unifex/nip.hpp>

#include <atomic>
#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <variant>

namespace unifex {

template<class NipOperation, size_t Index>
struct when_all_element_receiver;

namespace detail {

template <
    std::size_t Index,
    typename NipOperation,
    template<class > class AddQualifiers,
    typename... NipSenders>
struct when_all_operation_tuple;

template <std::size_t Index, class NipOperation, template<class > class AddQualifiers>
struct when_all_operation_tuple<Index, NipOperation, AddQualifiers> {
  template <typename Parent>
  explicit when_all_operation_tuple(Parent&) noexcept {}

  void start() noexcept {}
};

template <
    std::size_t Index,
    class NipOperation,
    template<class > class AddQualifiers,
    typename NipFirst,
    typename... NipRest>
struct when_all_operation_tuple<Index, NipOperation, AddQualifiers, NipFirst, NipRest...>
    : when_all_operation_tuple<Index + 1, NipOperation, AddQualifiers, NipRest...> {
  using first_t = AddQualifiers<unnip_t<NipFirst>>;
  template <typename Parent>
  explicit when_all_operation_tuple(
      Parent& parent,
      first_t&& first,
      AddQualifiers<unnip_t<NipRest>>&&... rest)
      : when_all_operation_tuple<Index + 1, NipOperation, AddQualifiers, NipRest...>{parent,
                                                               (AddQualifiers<unnip_t<NipRest>> &&)
                                                                   rest...},
        op_(connect((first_t &&) first, when_all_element_receiver<NipOperation, Index>{parent})) {}

  void start() noexcept {
    unifex::start(op_);
    when_all_operation_tuple<Index + 1, NipOperation, AddQualifiers, NipRest...>::start();
  }

 private:
  operation_t<first_t, when_all_element_receiver<NipOperation, Index>> op_;
};

} // namespace detail


template <class NipOperation, size_t Index>
struct when_all_element_receiver {
  using operation_ref_t = unnip_t<NipOperation>&;
  using receiver_t = typename unnip_t<NipOperation>::receiver_type;

  operation_ref_t op_;

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

  receiver_t& get_receiver() const noexcept { return op_.receiver_; }

  template <
      typename CPO,
      std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
  friend auto tag_invoke(CPO cpo, const when_all_element_receiver& r) noexcept(
      std::is_nothrow_invocable_v<CPO, const receiver_t&>)
      -> std::invoke_result_t<CPO, const receiver_t&> {
    return std::move(cpo)(std::as_const(r.get_receiver()));
  }

  inplace_stop_source& get_stop_source() const noexcept {
      return op_.stopSource_;
  }

  friend inplace_stop_token tag_invoke(
      tag_t<get_stop_token>,
      const when_all_element_receiver& r) noexcept {
    return r.get_stop_source().get_token();
  }

  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const when_all_element_receiver& r,
      Func&& func) {
    std::invoke(func, r.get_receiver());
  }
};

template <typename... NipSenders>
class when_all_sender {
 public:
  static_assert(sizeof...(NipSenders) > 0);

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<
      typename unnip_t<NipSenders>::template value_types<std::variant, std::tuple>...>>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      typename unnip_t<NipSenders>::template error_types<type_list>...,
      type_list<std::exception_ptr>>::template apply<Variant>;

  template <typename... Senders2>
  explicit when_all_sender(Senders2&&... senders)
      : senders_((Senders2 &&) senders...) {}

 private:
  template <typename NipReceiver, template<class > class AddQualifiers >
  struct operation {
    using receiver_type = unnip_t<NipReceiver>;

    struct cancel_operation {
      operation& op_;

      void operator()() noexcept {
        op_.stopSource_.request_stop();
      }
    };

    explicit operation(receiver_type&& receiver, AddQualifiers<unnip_t<NipSenders>>&&... senders)
        : receiver_((receiver_type &&) receiver),
          ops_(*this, (AddQualifiers<unnip_t<NipSenders>> &&) senders...) {}

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
        deliver_value(std::index_sequence_for<NipSenders...>{});
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

    template<class NipOperation, size_t Index>
    friend class when_all_element_receiver;

    std::tuple<std::optional<
        typename unnip_t<NipSenders>::template value_types<std::variant, std::tuple>>...>
        values_;
    std::optional<error_types<std::variant>> error_;
    std::atomic<std::size_t> refCount_{sizeof...(NipSenders)};
    std::atomic<bool> doneOrError_{false};
    inplace_stop_source stopSource_;
    UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
        receiver_type&>::template callback_type<cancel_operation>>
        stopCallback_;
    receiver_type receiver_;
    detail::when_all_operation_tuple<0, nip_t<operation>, AddQualifiers, NipSenders...> ops_;
  };

 public:
  template <typename Receiver>
  operation<nip_t<std::remove_cvref_t<Receiver>>, identity_t> connect(Receiver&& receiver) && {
    return std::apply([&](unnip_t<NipSenders>&&... senders) {
      return operation<nip_t<std::remove_cvref_t<Receiver>>, identity_t>{(Receiver &&) receiver,
                                                      (unnip_t<NipSenders> &&) senders...};
    }, std::move(senders_));
  }

  template <typename Receiver>
  operation<nip_t<std::remove_cvref_t<Receiver>>, std::add_lvalue_reference_t> connect(Receiver&& receiver) & {
    return std::apply([&](unnip_t<NipSenders>&... senders) {
      return operation<nip_t<std::remove_cvref_t<Receiver>>, std::add_lvalue_reference_t>{(Receiver &&) receiver,
                                                                    senders...};
    }, senders_);
  }

  template <typename Receiver>
  operation<nip_t<std::remove_cvref_t<Receiver>>, add_cvref_t> connect(Receiver&& receiver) const & {
    return std::apply([&](const unnip_t<NipSenders>&... senders) {
      return operation<nip_t<std::remove_cvref_t<Receiver>>, add_cvref_t>{(Receiver &&) receiver,
                                                                         senders...};
    }, senders_);
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

  std::tuple<unnip_t<NipSenders>...> senders_;
};

template <typename... Senders>
when_all_sender<nip_t<std::remove_cvref_t<Senders>>...> when_all(
    Senders&&... senders) {
  return when_all_sender<nip_t<std::remove_cvref_t<Senders>>...>{(Senders &&)
                                                              senders...};
}

} // namespace unifex
