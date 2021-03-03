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
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/with_query_value.hpp>

#include <atomic>
#include <cstdint>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _unnamed {

struct _op_base;

template <typename Receiver>
struct _operation {
  struct type;
};

template <typename Receiver>
using operation = typename _operation<Receiver>::type;

struct unnamed_primitive;

struct _sender {
  template <template <class...> class Variant,
            template <class...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <class...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  explicit _sender(unnamed_primitive& evt) noexcept
    : evt_(&evt) {}

  template (typename Receiver)
    (requires receiver_of<Receiver>)
  operation<remove_cvref_t<Receiver>> connect(Receiver&& r) const noexcept(
      noexcept(operation<remove_cvref_t<Receiver>>{std::declval<unnamed_primitive&>(), (Receiver&&)r})) {
    return operation<remove_cvref_t<Receiver>>{*evt_, (Receiver&&)r};
  }

 private:
  unnamed_primitive* evt_;
};

inline std::uintptr_t to_addr(const void* ptr) noexcept {
  return reinterpret_cast<std::uintptr_t>(ptr);
}

struct unnamed_primitive {
  unnamed_primitive() noexcept
    : unnamed_primitive(false) {}

  explicit unnamed_primitive(bool startSignalled) noexcept
    : state_(to_addr(startSignalled ? this : nullptr)) {}

  void set() noexcept;

  bool ready() const noexcept {
    return state_.load(std::memory_order_acquire) == to_addr(this);
  }

  void reset() noexcept {
    // transition from signalled-or-cancelled to not-signalled; do nothing if
    // neither signalled nor cancelled
    const std::uintptr_t signalledState = to_addr(this);
    const std::uintptr_t cancelledState = signalledState + 1;

    auto oldState = signalledState;

    do {
      if (state_.compare_exchange_weak(
          oldState,
          to_addr(nullptr),
          std::memory_order_acq_rel)) {
        // was either signalled or cancelled and is now nullptr
        return;
      }
    } while (oldState == signalledState || oldState == cancelledState);
  }

  [[nodiscard]] _sender async_wait() noexcept {
    return _sender{*this};
  }

 private:
  std::atomic<std::uintptr_t> state_{};

  template <typename T>
  friend struct _operation;

  void cancel(_op_base* op) noexcept;

  void start_or_wait(_op_base* op) noexcept;
};

struct _op_base {
  void (*completeImpl_)(_op_base*) noexcept;
  unnamed_primitive* evt_;

  explicit _op_base(unnamed_primitive& evt, void (*completeImpl)(_op_base*) noexcept) noexcept
    : evt_(&evt), completeImpl_(completeImpl) {}

  ~_op_base() = default;

  _op_base(_op_base&&) = delete;
  _op_base& operator=(_op_base&&) = delete;

  void complete() noexcept {
    completeImpl_(this);
  }
};

template <typename Receiver>
struct _receiver {
  struct type;
};

template <typename Receiver>
using receiver = typename _receiver<Receiver>::type;

template <typename Receiver>
struct _receiver<Receiver>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  void set_value() noexcept {
    if (unifex::get_stop_token(receiver_).stop_requested()) {
      set_done();
      return;
    }

    UNIFEX_TRY {
      unifex::set_value(std::move(receiver_));
    }
    UNIFEX_CATCH(...) {
      set_error(std::current_exception());
    }
  }

  void set_done() noexcept {
    unifex::set_done(std::move(receiver_));
  }

  void set_error(std::exception_ptr ptr) noexcept {
    unifex::set_error(std::move(receiver_), ptr);
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const type& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      Func&& func) {
    std::invoke(func, r.receiver_);
  }
};

template <typename Receiver>
struct _operation<Receiver>::type : private _op_base {
  explicit type(unnamed_primitive& evt, Receiver r)
      // TODO: noexceptness is incomplete
      noexcept(std::is_nothrow_move_constructible<Receiver>::value)
    : _op_base(evt, &complete_impl),
      stopToken_(unifex::get_stop_token(r)),
      op_(connect(schedule(), receiver<Receiver>{std::move(r)})) {}

  ~type() = default;

  type(type&&) = delete;
  type& operator=(type&&) = delete;

  void start() noexcept {
    // we could be cancelled or signalled anywhere between now through until we
    // return from start_or_wait, which means we need to be very careful here
    // that we don't invoke UB.  The dance between stop callback registration
    // and start_or_wait ensures that this stays valid through the call to
    // start_or_wait, but if we're signalled or cancelled before start_or_wait
    // returns then this will be on its way to being destroyed and will thus not
    // be safe to touch.

    // register for cancellation callbacks; we might get cancelled as a side-
    // effect of this, but, if we do, the callback will defer tear-down until
    // start_or_wait.
    callback_.construct(stopToken_, stop_callback{this});

    // either register for completion signals or complete immediately; immediate
    // completion will mean calling set_done rather than set_value if we lose a
    // race with cancellation.
    evt_->start_or_wait(this);

    // don't touch this anymore!
  }

 private:
  struct stop_callback {
    type* op_;

    void operator()() noexcept {
      op_->cancel();
    }
  };

  using stop_callback_t = typename stop_token_type_t<Receiver>::template callback_type<stop_callback>;

  stop_token_type_t<Receiver> stopToken_;
  UNIFEX_NO_UNIQUE_ADDRESS connect_result_t<decltype(schedule()), receiver<Receiver>> op_;
  UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<stop_callback_t> callback_;

  void cancel() noexcept {
    evt_->cancel(this);
  }

  static void complete_impl(_op_base* base) noexcept {
    auto self = static_cast<type*>(base);

    // resolve any potential races with cancellation
    self->callback_.destruct();

    unifex::start(self->op_);
  }
};

} // namespace _unnamed

using _unnamed::unnamed_primitive;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
