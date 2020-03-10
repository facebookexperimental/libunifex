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
namespace _when_all {

template <
    std::size_t Index,
    template <std::size_t> class Receiver,
    typename... Senders>
struct _operation_tuple {
  struct type;
};
template <
    std::size_t Index,
    template <std::size_t> class Receiver,
    typename... Senders>
using operation_tuple = typename _operation_tuple<Index, Receiver, Senders...>::type;

template <
    std::size_t Index,
    template <std::size_t> class Receiver,
    typename First,
    typename... Rest>
struct _operation_tuple<Index, Receiver, First, Rest...> {
  struct type;
};
template <
    std::size_t Index,
    template <std::size_t> class Receiver,
    typename First,
    typename... Rest>
struct _operation_tuple<Index, Receiver, First, Rest...>::type
  : operation_tuple<Index + 1, Receiver, Rest...> {
  template <typename Parent>
  explicit type(Parent& parent, First&& first, Rest&&... rest)
    : operation_tuple<Index + 1, Receiver, Rest...>{parent, (Rest &&) rest...},
      op_(connect((First &&) first, Receiver<Index>{parent})) {}

  void start() noexcept {
    unifex::start(op_);
    operation_tuple<Index + 1, Receiver, Rest...>::start();
  }

 private:
  operation_t<First, Receiver<Index>> op_;
};

template <std::size_t Index, template <std::size_t> class Receiver>
struct _operation_tuple<Index, Receiver> {
  struct type;
};
template <std::size_t Index, template <std::size_t> class Receiver>
struct _operation_tuple<Index, Receiver>::type {
  template <typename Parent>
  explicit type(Parent&) noexcept {}

  void start() noexcept {}
};

struct cancel_operation {
  inplace_stop_source& stopSource_;

  void operator()() noexcept {
    stopSource_.request_stop();
  }
};

template <template <typename...> class Variant, typename... Senders>
using error_types = typename concat_type_lists_unique_t<
    typename Senders::template error_types<type_list>...,
    type_list<std::exception_ptr>>::template apply<Variant>;

template <size_t Index, typename Operation>
struct _element_receiver {
  struct type;
};
template <size_t Index, typename Operation>
using element_receiver = typename _element_receiver<Index, Operation>::type;

template <size_t Index, typename Operation>
struct _element_receiver<Index, Operation>::type final {
  using element_receiver = type;
  Operation& op_;
  using receiver_type = typename Operation::receiver_type;

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

  receiver_type& get_receiver() const { return op_.receiver_; }

  template <
      typename CPO,
      typename R,
      typename... Args,
      std::enable_if_t<
        std::conjunction_v<
          std::negation<is_receiver_cpo<CPO>>,
          std::is_same<R, element_receiver>,
          std::is_invocable<CPO, const receiver_type&, Args...>>, int> = 0>
  friend auto tag_invoke(CPO cpo, const R& r, Args&&... args) noexcept(
      std::is_nothrow_invocable_v<CPO, const receiver_type&, Args...>)
      -> std::invoke_result_t<CPO, const receiver_type&, Args...> {
    return std::move(cpo)(std::as_const(r.get_receiver()), (Args&&)args...);
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

template <typename Receiver, typename... Senders>
struct _op {
  struct type;
};
template <typename Receiver, typename... Senders>
using operation = typename _op<std::remove_cvref_t<Receiver>, Senders...>::type;

template <typename Receiver, typename... Senders>
struct _op<Receiver, Senders...>::type {
  using operation = type;
  using receiver_type = Receiver;
  template<std::size_t Index, typename Operation>
  friend class _element_receiver;

  explicit type(Receiver&& receiver, Senders&&... senders)
    : receiver_((Receiver &&) receiver),
      ops_(*this, (Senders &&) senders...) {}

  void start() noexcept {
    stopCallback_.construct(
        get_stop_token(receiver_), cancel_operation{stopSource_});
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
  std::optional<error_types<std::variant, Senders...>> error_;
  std::atomic<std::size_t> refCount_{sizeof...(Senders)};
  std::atomic<bool> doneOrError_{false};
  inplace_stop_source stopSource_;
  UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
      Receiver&>::template callback_type<cancel_operation>>
      stopCallback_;
  Receiver receiver_;
  template<std::size_t Index>
  using element_receiver = element_receiver<Index, operation>;
  operation_tuple<0, element_receiver, Senders...> ops_;
};

template <typename... Senders>
struct _sender {
  class type;
};
template <typename... Senders>
using sender = typename _sender<std::remove_cvref_t<Senders>...>::type;

template <typename... Senders>
class _sender<Senders...>::type {
  using sender = type;
 public:
  static_assert(sizeof...(Senders) > 0);

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<
      typename Senders::template value_types<std::variant, std::tuple>...>>;

  template <template <typename...> class Variant>
  using error_types = error_types<Variant, Senders...>;

  template <typename... Senders2>
  explicit type(Senders2&&... senders)
    : senders_((Senders2 &&) senders...) {}

  template <typename Receiver>
  operation<Receiver, Senders...> connect(Receiver&& receiver) && {
    return std::apply([&](Senders&&... senders) {
      return operation<Receiver, Senders...>{
          (Receiver &&) receiver, (Senders &&) senders...};
    }, std::move(senders_));
  }

 private:

  // Customise the 'blocking' CPO to combine the blocking-nature
  // of each of the child operations.
  friend blocking_kind tag_invoke(tag_t<blocking>, const sender& s) noexcept {
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
} // namespace _when_all

namespace _when_all_cpo {
  inline constexpr struct _fn {
    template <typename... Senders>
    auto operator()(Senders&&... senders) const 
        -> _when_all::sender<Senders...> {
      return _when_all::sender<Senders...>{(Senders &&) senders...};
    }
  } when_all{};
} // namespace _when_all_cpo

using _when_all_cpo::when_all;

} // namespace unifex
