/*
 * Copyright (c) Meta Platforms, Inc. and its affiliates.
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

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

#include <unifex/blocking.hpp>
#include <unifex/continuations.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/type_list.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _when_all_range {

template <typename Receiver, typename Sender>
struct _operation final {
  struct type;
};

template <typename Receiver, typename Sender>
struct _element_receiver final {
  struct type;
};

template <typename Receiver, typename Sender>
using element_receiver = typename _element_receiver<Receiver, Sender>::type;

template <typename Receiver, typename Sender>
using operation =
    typename _operation<unifex::remove_cvref_t<Receiver>, Sender>::type;

template <typename Receiver, typename Sender>
struct _operation<Receiver, Sender>::type final {
private:
  using sender_nonvoid_value_type =
      unifex::sender_single_value_result_t<unifex::remove_cvref_t<Sender>>;

  using element_receiver_t = element_receiver<Receiver, Sender>;

  friend struct _element_receiver<Receiver, Sender>::type;

  struct cancel_operation final {
    type& op_;
    void operator()() noexcept { op_.request_stop(); }
  };

  struct _operation_holder final {
    std::optional<sender_nonvoid_value_type> value;
    unifex::connect_result_t<Sender, element_receiver_t> connection;

    _operation_holder(Sender&& sender, type& op, std::size_t index) noexcept(
        unifex::is_nothrow_connectable_v<Sender, element_receiver_t>&& std::
            is_nothrow_constructible_v<element_receiver_t, type&, std::size_t>)
      : connection(unifex::connect(
            static_cast<Sender&&>(sender), element_receiver_t(op, index))) {}
  };

public:
  type(Receiver&& receiver, std::vector<Sender>&& senders)
    : receiver_(std::move(receiver))
    , refCount_(senders.size()) {
    std::allocator<_operation_holder> allocator;
    holders_ = allocator.allocate(senders.size());
    try {
      for (auto&& sender : senders) {
        new (holders_ + numHolders_)
            _operation_holder{std::move(sender), *this, numHolders_};
        ++numHolders_;
      }
    } catch (...) {
      std::destroy(
          std::make_reverse_iterator(holders_ + numHolders_),
          std::make_reverse_iterator(holders_));
      allocator.deallocate(holders_, senders.size());
      holders_ = nullptr;
      throw;
    }
  }

  type(type&&) = delete;

  // does not run when constructor throws, numHolders_ is the correct size
  ~type() {
    std::allocator<_operation_holder> allocator;
    std::destroy(
        std::make_reverse_iterator(holders_ + numHolders_),
        std::make_reverse_iterator(holders_));
    allocator.deallocate(holders_, numHolders_);
  }

  void start() noexcept {
    if (numHolders_ == 0) {
      // In case there is 0 sender, immediately complete
      unifex::set_value(
          std::move(receiver_), std::vector<sender_nonvoid_value_type>{});
    } else {
      stopCallback_.construct(
          unifex::get_stop_token(receiver_), cancel_operation{*this});
      // last start() might destroy this
      std::for_each(
          holders_, holders_ + numHolders_, [](auto& holder) noexcept {
            unifex::start(holder.connection);
          });
    }
  }

  void request_stop() noexcept {
    if (refCount_.fetch_add(1, std::memory_order_relaxed) == 0) {
      // deliver_result already called
      return;
    }
    stopSource_.request_stop();

    element_complete();
  }

  void element_complete() noexcept {
    if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      stopCallback_.destruct();

      if (doneOrError_.load(std::memory_order_relaxed)) {
        if (error_.has_value()) {
          std::visit(
              [this](auto&& error) {
                unifex::set_error(
                    std::move(receiver_), std::forward<decltype(error)>(error));
              },
              std::move(error_.value()));
        } else {
          unifex::set_done(std::move(receiver_));
        }
      } else {
        UNIFEX_TRY {
          std::vector<sender_nonvoid_value_type> values;
          values.reserve(numHolders_);
          std::transform(
              holders_,
              holders_ + numHolders_,
              std::back_inserter(values),
              [](auto&& h) -> decltype(auto) {
                return std::move(h.value.value());
              });
          unifex::set_value(std::move(receiver_), std::move(values));
        }
        UNIFEX_CATCH(...) {
          unifex::set_error(std::move(receiver_), std::current_exception());
        }
      }
    }
  }

  _operation_holder* holders_;
  std::size_t numHolders_{0};
  UNIFEX_NO_UNIQUE_ADDRESS
  std::optional<unifex::sender_error_types_t<Sender, std::variant>>
      error_;
  UNIFEX_NO_UNIQUE_ADDRESS
  unifex::manual_lifetime<typename unifex::stop_token_type_t<
      Receiver&>::template callback_type<cancel_operation>>
      stopCallback_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  std::atomic<std::size_t> refCount_;
  std::atomic<bool> doneOrError_{false};
  unifex::inplace_stop_source stopSource_;
};

template <typename Receiver, typename Sender>
struct _element_receiver<Receiver, Sender>::type final {
  operation<Receiver, Sender>& op_;
  size_t index_;

  type(operation<Receiver, Sender>& op, size_t index) noexcept
    : op_(op)
    , index_(index){};

  template <typename... Value>
  void set_value(Value&&... value) noexcept {
    UNIFEX_TRY {
      op_.holders_[index_].value.emplace(std::forward<Value>(value)...);
      op_.element_complete();
    }
    UNIFEX_CATCH(...) { this->set_error(std::current_exception()); }
  }

  template <typename Error>
  void set_error(Error&& error) noexcept {
    if (!op_.doneOrError_.exchange(true, std::memory_order_relaxed)) {
      op_.error_.emplace(
          std::in_place_type_t<std::decay_t<Error>>{}, (Error &&) error);
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

  [[nodiscard]] Receiver& get_receiver() const noexcept {
    return op_.receiver_;
  }

  template(typename CPO, typename R)                         //
      (requires unifex::is_receiver_query_cpo_v<CPO> AND     //
           unifex::same_as<R, type> AND                      //
               unifex::is_callable_v<CPO, const Receiver&>)  //
      friend auto tag_invoke(CPO cpo, const R& r)            //
      noexcept(unifex::is_nothrow_callable_v<CPO, const Receiver&>)
          -> unifex::callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.get_receiver()));
  }

  [[nodiscard]] unifex::inplace_stop_source& get_stop_source() const noexcept {
    return op_.stopSource_;
  }

  friend unifex::inplace_stop_token tag_invoke(
      unifex::tag_t<unifex::get_stop_token> /* unused */,
      const type& r) noexcept {
    return r.get_stop_source().get_token();
  }

  template <typename Func>
  friend void tag_invoke(
      unifex::tag_t<unifex::visit_continuations> /* unused */,
      const type& r,
      Func&& func) noexcept {
    std::invoke(func, r.get_receiver());
  }
};

// Sender adapter for vector<Sender>
template <typename Sender>
struct _sender final {
  struct type;
};

template <typename Sender>
using sender = typename _sender<Sender>::type;

template <typename Sender>
struct _sender<Sender>::type final {
public:
  using sender_value_type =
      unifex::sender_single_value_result_t<unifex::remove_cvref_t<Sender>>;

  explicit type(std::vector<Sender>&& senders) noexcept(
      std::is_nothrow_move_constructible_v<std::vector<Sender>>)
    : senders_(std::move(senders)) {}

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<std::vector<sender_value_type>>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

private:
  template(typename Sender2, typename Receiver)  //
      (requires unifex::same_as<unifex::remove_cvref_t<Sender2>, type> AND
           unifex::receiver<Receiver>)  //
      friend auto tag_invoke(
          unifex::tag_t<unifex::connect> /* unused */,
          Sender2&& sender,
          Receiver&& receiver) {
    // return an operation that wraps all connections
    return operation<unifex::remove_cvref_t<Receiver>, Sender>{
        static_cast<Receiver&&>(receiver),
        std::move((static_cast<Sender2&&>(sender)).senders_)};
  }

  // Combine the blocking-nature of each of the child operations.
  friend constexpr auto tag_invoke(
      unifex::tag_t<unifex::blocking> /* unused */, const type&) noexcept {
    if constexpr (
        unifex::cblocking<Sender>() == unifex::blocking_kind::always_inline ||
        unifex::cblocking<Sender>() == unifex::blocking_kind::always) {
      return unifex::cblocking<Sender>();
    } else {
      // we complete inline if the input is empty so every other case is "maybe"
      return unifex::blocking_kind::maybe;
    }
  }

  std::vector<Sender> senders_;
};

namespace _cpo {
struct _fn final {
  template(typename Sender)               //
      (requires(unifex::sender<Sender>))  //
      auto
      operator()(std::vector<Sender> senders) const {
    return _when_all_range::sender<Sender>(std::move(senders));
  }
  template <typename Iterator>
  auto operator()(Iterator first, Iterator last) const -> decltype(operator()(
      std::vector<typename std::iterator_traits<Iterator>::value_type>{
          first, last})) {
    return operator()(
        std::vector<typename std::iterator_traits<Iterator>::value_type>{
            first, last});
  }
};
}  // namespace _cpo
}  // namespace _when_all_range

inline constexpr _when_all_range::_cpo::_fn when_all_range{};
}  // namespace unifex
#include <unifex/detail/epilogue.hpp>
