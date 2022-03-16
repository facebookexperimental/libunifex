/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
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
#include <unifex/std_concepts.hpp>
#include <unifex/optional.hpp>
#include <unifex/utility.hpp>
#include <unifex/variant.hpp>

#include <atomic>
#include <cstddef>
#include <tuple>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

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
  connect_result_t<First, Receiver<Index>> op_;
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

template <typename Receiver, typename... Senders>
struct _op {
  struct type;
};
template <typename Receiver, typename... Senders>
using operation = typename _op<remove_cvref_t<Receiver>, Senders...>::type;

template <typename... Errors>
using unique_decayed_error_types = concat_type_lists_unique_t<
  type_list<std::decay_t<Errors>>...>;

template <template <typename...> class Variant, typename... Senders>
using error_types =
    typename concat_type_lists_unique_t<
        sender_error_types_t<Senders, unique_decayed_error_types>...,
        type_list<std::exception_ptr>>::template apply<Variant>;

template <typename... Values>
using decayed_value_tuple = type_list<std::tuple<std::decay_t<Values>...>>;

template <typename Sender>
using value_variant_for_sender =
  typename sender_value_types_t<Sender, concat_type_lists_unique_t, decayed_value_tuple>
      ::template apply<variant>;

template <size_t Index, typename Receiver, typename... Senders>
struct _element_receiver {
  struct type;
};
template <size_t Index, typename Receiver, typename... Senders>
using element_receiver = typename _element_receiver<Index, Receiver, Senders...>::type;

template <size_t Index, typename Receiver, typename... Senders>
struct _element_receiver<Index, Receiver, Senders...>::type final {
  using element_receiver = type;

  operation<Receiver, Senders...>& op_;

  template <typename... Values>
  void set_value(Values&&... values) noexcept {
    UNIFEX_TRY {
      std::get<Index>(op_.values_)
          .emplace(
              in_place_type_t<std::tuple<std::decay_t<Values>...>>{},
              (Values &&) values...);
      op_.element_complete();
    } UNIFEX_CATCH (...) {
      this->set_error(std::current_exception());
    }
  }

  template <typename Error>
  void set_error(Error&& error) noexcept {
    if (!op_.doneOrError_.exchange(true, std::memory_order_relaxed)) {
      op_.error_.emplace(in_place_type_t<std::decay_t<Error>>{}, (Error &&) error);
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

  template(typename CPO, typename R)
      (requires is_receiver_query_cpo_v<CPO> AND
          same_as<R, element_receiver> AND
          is_callable_v<CPO, const Receiver&>)
  friend auto tag_invoke(CPO cpo, const R& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
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

template <typename Receiver, typename... Senders>
struct _op<Receiver, Senders...>::type {
  using operation = type;
  using receiver_type = Receiver;
  template <std::size_t Index, typename Receiver2, typename... Senders2>
  friend struct _element_receiver;

  template <typename Receiver2, typename... Senders2>
  explicit type(Receiver2&& receiver, Senders2&&... senders)
    : receiver_((Receiver2 &&) receiver),
      ops_(*this, (Senders2 &&) senders...) {}

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
        unifex::visit(
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
    UNIFEX_TRY {
      unifex::set_value(
          std::move(receiver_),
          std::get<Indices>(std::move(values_)).value()...);
    } UNIFEX_CATCH (...) {
      unifex::set_error(std::move(receiver_), std::current_exception());
    }
  }

  std::tuple<optional<value_variant_for_sender<remove_cvref_t<Senders>>>...> values_;
  optional<error_types<variant, remove_cvref_t<Senders>...>> error_;
  std::atomic<std::size_t> refCount_{sizeof...(Senders)};
  std::atomic<bool> doneOrError_{false};
  inplace_stop_source stopSource_;
  UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
      Receiver&>::template callback_type<cancel_operation>>
      stopCallback_;
  Receiver receiver_;
  template <std::size_t Index>
  using op_element_receiver = element_receiver<Index, Receiver, Senders...>;
  operation_tuple<0, op_element_receiver, Senders...> ops_;
};

template <typename... Senders>
struct _sender {
  class type;
};
template <typename... Senders>
using sender = typename _sender<remove_cvref_t<Senders>...>::type;

template <typename Receiver, typename Indices, typename... Senders>
extern const bool _when_all_connectable_v;

template <typename Receiver, std::size_t... Indices, typename... Senders>
inline constexpr bool _when_all_connectable_v<Receiver, std::index_sequence<Indices...>, Senders...> =
  (sender_to<Senders, element_receiver<Indices, Receiver, Senders...>> &&...);

template <typename Receiver, typename... Senders>
inline constexpr bool when_all_connectable_v =
  _when_all_connectable_v<Receiver, std::index_sequence_for<Senders...>, Senders...>;

template <typename... Senders>
class _sender<Senders...>::type {
  using sender = type;
 public:
  static_assert(sizeof...(Senders) > 0);

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<value_variant_for_sender<Senders>...>>;

  template <template <typename...> class Variant>
  using error_types = error_types<Variant, Senders...>;

  static constexpr bool sends_done = true;

  template <typename... Senders2>
  explicit type(Senders2&&... senders)
    : senders_((Senders2 &&) senders...) {}

  template(typename CPO, typename Sender, typename Receiver)
      (requires same_as<CPO, tag_t<unifex::connect>> AND
        same_as<remove_cvref_t<Sender>, type> AND
        when_all_connectable_v<remove_cvref_t<Receiver>, member_t<Sender, Senders>...>)
  friend auto tag_invoke([[maybe_unused]] CPO cpo, Sender&& sender, Receiver&& receiver)
    -> operation<Receiver, member_t<Sender, Senders>...> {
    return std::apply([&](auto&&... senders) {
      return operation<Receiver, member_t<Sender, Senders>...>{
          (Receiver &&) receiver, static_cast<decltype(senders)>(senders)...};
    }, static_cast<Sender &&>(sender).senders_);
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

namespace _cpo {
  struct _fn {
    template (typename... Senders)
      (requires (unifex::sender<Senders> &&...) AND tag_invocable<_fn, Senders...>)
    auto operator()(Senders&&... senders) const
        -> tag_invoke_result_t<_fn, Senders...> {
      return tag_invoke(*this, (Senders &&) senders...);
    }
    template (typename... Senders)
      (requires (typed_sender<Senders> &&...) AND (!tag_invocable<_fn, Senders...>))
    auto operator()(Senders&&... senders) const
        -> _when_all::sender<Senders...> {
      return _when_all::sender<Senders...>{(Senders &&) senders...};
    }
  };
} // namespace _cpo
} // namespace _when_all

inline constexpr _when_all::_cpo::_fn when_all{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
