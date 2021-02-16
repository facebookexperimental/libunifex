/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <unifex/config.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/with_query_value.hpp>

#include <atomic>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _amre {

struct _op_base;

template <typename Receiver>
struct _operation {
  struct type;
};

template <typename Receiver>
using operation = typename _operation<Receiver>::type;

struct async_manual_reset_event;

struct _sender {
  template <template <class...> class Variant,
            template <class...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <class...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = false;

  explicit _sender(async_manual_reset_event& evt) noexcept
    : evt_(&evt) {}

  template (typename Receiver)
    (requires receiver_of<Receiver>)
  operation<remove_cvref_t<Receiver>> connect(Receiver&& r) const noexcept(
      std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>) {
    return operation<remove_cvref_t<Receiver>>{*evt_, (Receiver&&)r};
  }

 private:
  async_manual_reset_event* evt_;
};

struct async_manual_reset_event {
  async_manual_reset_event() noexcept
    : async_manual_reset_event(false) {}

  explicit async_manual_reset_event(bool startSignalled) noexcept
    : state_(startSignalled ? this : nullptr) {}

  void set() noexcept;

  bool ready() const noexcept {
    return state_.load(std::memory_order_acquire) ==
        static_cast<const void*>(this);
  }

  void reset() noexcept {
    // transition from signalled (i.e. state_ == this) to not-signalled
    // (i.e. state_ == nullptr).
    void* oldState = this;

    // We can ignore the the result.  We're using _strong so it won't fail
    // spuriously; if it fails, it means it wasn't previously in the signalled
    // state so resetting is a no-op.
    (void)state_.compare_exchange_strong(
        oldState, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed);
  }

  [[nodiscard]] _sender async_wait() noexcept {
    return _sender{*this};
  }

 private:
  std::atomic<void*> state_{};

  friend struct _op_base;

  static void start_or_wait(_op_base& op, async_manual_reset_event& evt) noexcept;
};

struct _op_base {
  // note: next_ is intentionally left indeterminate until the operation is
  //       pushed on the event's stack of waiting operations
  //
  // note: next_ is the first member so that list operations don't have to
  //       offset into this struct; hopefully that leads to smaller code
  _op_base* next_;
  async_manual_reset_event* evt_;
  void (*setValue_)(_op_base*) noexcept;

  explicit _op_base(async_manual_reset_event& evt, void (*setValue)(_op_base*) noexcept)
    : evt_(&evt), setValue_(setValue) {}

  ~_op_base() = default;

  _op_base(_op_base&&) = delete;
  _op_base& operator=(_op_base&&) = delete;

  void set_value() noexcept {
    setValue_(this);
  }

  void start() noexcept {
    async_manual_reset_event::start_or_wait(*this, *evt_);
  }
};

template <typename Receiver>
struct _operation<Receiver>::type : private _op_base {
  explicit type(async_manual_reset_event& evt, Receiver r)
      noexcept(std::is_nothrow_move_constructible_v<Receiver>)
    : _op_base(evt, &set_value_impl),
      op_(create_op(std::move(r))) {}

  ~type() = default;

  type(type&&) = delete;
  type& operator=(type&&) = delete;

  using _op_base::start;

 private:
  static auto create_op(Receiver&& r) {
    return connect(
        with_query_value(schedule(), get_stop_token, unstoppable_token{}),
        std::move(r));
  }

  UNIFEX_NO_UNIQUE_ADDRESS decltype(create_op(std::declval<Receiver>())) op_;

  static void set_value_impl(_op_base* base) noexcept {
    auto self = static_cast<type*>(base);

    unifex::start(self->op_);
  }
};

} // namespace _amre

using _amre::async_manual_reset_event;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
