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

#include <unifex/config.hpp>
#include <unifex/async_manual_reset_event.hpp>
#include <unifex/just_from.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/sequence.hpp>
#include <unifex/type_traits.hpp>

#include <atomic>
#include <cstddef>
#include <memory>

#include <unifex/detail/prologue.hpp>

namespace unifex::v2 {

namespace _async_scope {

template <typename Sender>
struct _nest_sender final {
  struct type;
};

template <typename Sender>
using nest_sender = typename _nest_sender<Sender>::type;

struct async_scope;

struct scope_reference final {
  scope_reference() noexcept = default;

  explicit scope_reference(async_scope* scope) noexcept
    : scope_(scope_or_nullptr(scope)) {}

  scope_reference(scope_reference&& other) noexcept
    : scope_(std::exchange(other.scope_, nullptr)) {}

  scope_reference(const scope_reference& other) noexcept
    : scope_reference(other.scope_) {}

  ~scope_reference();

  scope_reference& operator=(scope_reference rhs) noexcept {
    std::swap(scope_, rhs.scope_);
    return *this;
  }

  explicit operator bool() const noexcept { return scope_ != nullptr; }

private:
  async_scope* scope_ = nullptr;

  static async_scope* scope_or_nullptr(async_scope* scope) noexcept;
};

struct async_scope final {
  async_scope() noexcept = default;

  async_scope(async_scope&&) = delete;

  ~async_scope() {
    UNIFEX_ASSERT(join_started());
    UNIFEX_ASSERT(use_count() == 0);
  }

  [[nodiscard]] auto join() noexcept {
    return sequence(
        just_from([this]() noexcept { end_scope(); }), evt_.async_wait());
  }

  // Equivalent to, but more efficient than, join_started() && use_count() == 0
  bool joined() const noexcept {
    auto state = opState_.load(std::memory_order_relaxed);
    return state == 0u;
  }

  template(typename Sender)      //
      (requires sender<Sender>)  //
      [[nodiscard]] auto nest(Sender&& sender) noexcept(
          std::is_nothrow_constructible_v<
              nest_sender<remove_cvref_t<Sender>>,
              Sender,
              scope_reference>) {
    if (scope_reference scope{this}) {
      return nest_sender<remove_cvref_t<Sender>>{
          static_cast<Sender&&>(sender), std::move(scope)};
    } else {
      return nest_sender<remove_cvref_t<Sender>>{};
    }
  }

  bool join_started() const noexcept {
    auto state = opState_.load(std::memory_order_relaxed);
    return scope_ended(state);
  }

  std::size_t use_count() const noexcept {
    auto state = opState_.load(std::memory_order_relaxed);
    return use_count(state);
  }

private:
  static constexpr std::size_t scopeEndedBit{1u};

  /**
   * Returns true if the given state is marked with "stopping", indicating that
   * no more work may be spawned within the scope.
   */
  static bool scope_ended(std::size_t state) noexcept {
    return (state & scopeEndedBit) == 0u;
  }

  /**
   * Returns the number of outstanding operations in the scope.
   */
  static std::size_t use_count(std::size_t state) noexcept {
    return state >> 1;
  }

  // (opState_ & 1) is 1 until this scope has been ended
  // (opState_ >> 1) is the number of outstanding operations
  std::atomic<std::size_t> opState_{1u};
  async_manual_reset_event evt_;

  /**
   * Marks the scope to prevent nest from starting any new work.
   */
  void end_scope() noexcept {
    // prevent new work from being nested within this scope; by clearing the
    // scopeEndedBit, we cause try_record_start() to fail because the scope has
    // ended
    auto oldState =
        opState_.fetch_and(~scopeEndedBit, std::memory_order_acq_rel);

    if (use_count(oldState) == 0) {
      // there are no outstanding operations to wait for
      evt_.set();
    }
  }

  friend void record_completion(async_scope* scope) noexcept {
    auto oldState = scope->opState_.fetch_sub(2u, std::memory_order_acq_rel);

    if (scope_ended(oldState) && use_count(oldState) == 1u) {
      // the scope is stopping and we're the last op to finish
      scope->evt_.set();
    }
  }

  [[nodiscard]] friend bool try_record_start(async_scope* scope) noexcept {
    auto opState = scope->opState_.load(std::memory_order_relaxed);

    do {
      if (scope_ended(opState)) {
        return false;
      }

      UNIFEX_ASSERT(opState + 2u > opState);
    } while (!scope->opState_.compare_exchange_weak(
        opState, opState + 2u, std::memory_order_relaxed));

    return true;
  }

  friend void end_scope(async_scope& scope) noexcept { scope.end_scope(); }
};

template <typename Sender, typename Receiver>
struct _nest_op final {
  struct type;
};

template <typename Sender, typename Receiver>
using nest_op = typename _nest_op<Sender, Receiver>::type;

template <typename Sender, typename Receiver>
struct _nest_op<Sender, Receiver>::type final {
  template <typename Sender2, typename Receiver2>
  explicit type(Sender2&& s, Receiver2&& r, scope_reference&& scope) noexcept(
      is_nothrow_connectable_v<Sender2, Receiver2>)
    : scope_(std::move(scope)) {
    UNIFEX_ASSERT(scope_);
    activate_union_member_with(op_, [&]() {
      return unifex::connect(
          static_cast<Sender2&&>(s), static_cast<Receiver2&&>(r));
    });
  }

  explicit type(Receiver&& r) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>) {
    activate_union_member(receiver_, std::move(r));
  }

  explicit type(const Receiver& r) noexcept(
      std::is_nothrow_copy_constructible_v<Receiver>) {
    activate_union_member(receiver_, r);
  }

  type(type&& op) = delete;

  ~type() {
    if (scope_) {
      deactivate_union_member(op_);
    } else {
      deactivate_union_member(receiver_);
    }
  }

  friend void tag_invoke(tag_t<start>, type& op) noexcept {
    if (op.scope_) {
      unifex::start(op.op_.get());
    } else {
      unifex::set_done(std::move(op).receiver_.get());
    }
  }

private:
  using op_t = connect_result_t<Sender, Receiver>;

  scope_reference scope_;
  union {
    UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<Receiver> receiver_;
    UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<op_t> op_;
  };
};

template <typename Sender>
struct _nest_sender<Sender>::type final {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = sender_value_types_t<Sender, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<Sender, Variant>;

  static constexpr bool sends_done = true;

  type() noexcept = default;

  template <typename Sender2>
  explicit type(Sender2&& sender, scope_reference&& scope) noexcept(
      std::is_nothrow_constructible_v<Sender, Sender2>)
    : scope_(std::move(scope)) {
    UNIFEX_ASSERT(scope_);
    sender_.construct(static_cast<Sender2&&>(sender));
  }

  type(const type& t) noexcept(std::is_nothrow_copy_constructible_v<Sender>)
    : scope_(t.scope_) {
    if (scope_) {
      sender_.construct(t.sender_.get());
    }
  }

  type(type&& t) noexcept(std::is_nothrow_move_constructible_v<Sender>)
    : scope_(std::move(t).scope_) {
    if (scope_) {
      sender_.construct(std::move(t).sender_.get());
      t.sender_.destruct();
    }
  }

  ~type() {
    if (scope_) {
      sender_.destruct();
    }
  }

  // helper to compute the noexcept clause for connect
  template <typename S, typename Receiver>
  static constexpr bool nothrow_connect =
      // we either construct a next_op from our wrapped sender, the receiver,
      // and a scope reference
      std::is_nothrow_constructible_v<
          nest_op<Sender, remove_cvref_t<Receiver>>,
          // this applies the cvref-ness of *this to the Sender type
          decltype(std::declval<S>().sender_.get()),
          Receiver,
          scope_reference>
          // or we construct a next_op from the receiver
          && std::is_nothrow_constructible_v<
              nest_op<Sender, remove_cvref_t<Receiver>>,
              Receiver>;

  template(typename Receiver)                                 //
      (requires sender_to<Sender, remove_cvref_t<Receiver>>)  //
      friend auto tag_invoke(tag_t<connect>, type&& s, Receiver&& r) noexcept(
          nothrow_connect<type, Receiver>)
          -> nest_op<Sender, remove_cvref_t<Receiver>> {
    auto scope = std::move(s).scope_;

    if (scope) {
      // since we've nulled out the sender's scope, its destructor won't clean
      // up the sender_ member, which means we have to do it here, but not until
      // after we construct our return value
      scope_guard destroySender = [&s]() noexcept {
        s.sender_.destruct();
      };

      return nest_op<Sender, remove_cvref_t<Receiver>>{
          std::move(s).sender_.get(),
          static_cast<Receiver&&>(r),
          std::move(scope)};
    } else {
      return nest_op<Sender, remove_cvref_t<Receiver>>{
          static_cast<Receiver&&>(r)};
    }
  }

  template(typename Receiver)                                        //
      (requires sender_to<const Sender&, remove_cvref_t<Receiver>>)  //
      friend auto tag_invoke(
          tag_t<connect>,
          const type& s,
          Receiver&& r) noexcept(nothrow_connect<const type&, Receiver>)
          -> nest_op<Sender, remove_cvref_t<Receiver>> {
    // make a copy of the scope_reference, which will try to record the start of
    // a new nested operation
    auto scope = s.scope_;

    if (scope) {
      return nest_op<Sender, remove_cvref_t<Receiver>>{
          s.sender_.get(), static_cast<Receiver&&>(r), std::move(scope)};
    } else {
      return nest_op<Sender, remove_cvref_t<Receiver>>{
          static_cast<Receiver&&>(r)};
    }
  }

  friend constexpr auto
  tag_invoke(tag_t<unifex::blocking>, const type&) noexcept {
    if constexpr (
        cblocking<Sender>() == blocking_kind::always_inline ||
        cblocking<Sender>() == blocking_kind::always) {
      return cblocking<Sender>();
    } else {
      return blocking_kind::maybe;
    }
  }

private:
  scope_reference scope_;
  UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<Sender> sender_;
};

inline scope_reference::~scope_reference() {
  if (scope_ != nullptr) {
    record_completion(scope_);
  }
}

inline async_scope*
scope_reference::scope_or_nullptr(async_scope* scope) noexcept {
  if (scope != nullptr && try_record_start(scope)) {
    return scope;
  }

  return nullptr;
}

}  // namespace _async_scope

using _async_scope::async_scope;

}  // namespace unifex::v2

#include <unifex/detail/epilogue.hpp>
