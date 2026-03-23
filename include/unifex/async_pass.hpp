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

#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime_union.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>

#include <unifex/cancellable.hpp>
#include <unifex/detail/completion_forwarder.hpp>
#include <unifex/detail/concept_macros.hpp>

#include <optional>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _pass {

template <typename... Args>
inline constexpr bool constructible_v =
    (std::is_constructible_v<std::decay_t<Args>, Args> && ...);

template <typename... Args>
inline constexpr bool nothrow_constructible_v =
    (std::is_nothrow_constructible_v<std::decay_t<Args>, Args> && ...);

struct accept_op_base_noargs {
  void rethrow(std::exception_ptr ex) { (*set_error_)(this, std::move(ex)); }

  void (*unlocked_complete_)(accept_op_base_noargs*) noexcept {nullptr};
  void (*set_error_)(accept_op_base_noargs*, std::exception_ptr){nullptr};
};

template <bool Noexcept, typename... Args>
struct accept_op_callable;

template <typename... Args>
struct accept_op_base : public accept_op_base_noargs {
  template <bool Noexcept>
  using callable = accept_op_callable<Noexcept, Args...>;

  void (*set_value_)(accept_op_base*, Args&&...);
};

template <typename... Args>
struct accept_op_callable<false, Args...> : public accept_op_base<Args...> {
  void operator()(Args&&... args) {
    (*this->set_value_)(this, std::forward<Args>(args)...);
  }
};

template <typename... Args>
struct accept_op_callable<true, Args...> : public accept_op_base<Args...> {
  void operator()(Args&&... args) noexcept {
    (*reinterpret_cast<void (*)(accept_op_base<Args...>*, Args&&...) noexcept>(
        this->set_value_))(this, std::forward<Args>(args)...);
  }
};

template <typename AcceptorFn>
struct immediate_accept {
  template <typename... Args>
  struct op : public accept_op_base<Args...> {
    explicit op(AcceptorFn&& acceptorFn, std::true_type) noexcept
      : acceptorFn_(std::forward<AcceptorFn>(acceptorFn)) {
      this->set_value_ = [](accept_op_base<Args...>* self, Args&&... args) {
        static_cast<op*>(self)->acceptorFn_(std::forward<Args>(args)...);
      };
    }

    explicit op(AcceptorFn&& acceptorFn, std::false_type) noexcept
      : op(std::forward<AcceptorFn>(acceptorFn), std::true_type{}) {
      this->set_error_ = [](accept_op_base_noargs* /* self */,
                            std::exception_ptr ex) {
        std::rethrow_exception(std::move(ex));
      };
    }

    AcceptorFn acceptorFn_;
  };

  template <typename ArgsList>
  using type = apply_to_type_list_t<op, ArgsList>;
};

template <bool Noexcept, typename ArgsList, typename AcceptorFn>
auto accept_call_with(AcceptorFn&& acceptorFn) noexcept {
  return typename immediate_accept<AcceptorFn>::template type<ArgsList>{
      std::forward<AcceptorFn>(acceptorFn),
      std::integral_constant<bool, Noexcept>{}};
}

template <bool Noexcept>
struct call_or_throw_op_base;

template <>
struct call_or_throw_op_base<false> {
  void call(accept_op_base_noargs& acceptor);

  void (*unlocked_complete_)(call_or_throw_op_base*) noexcept {nullptr};
  void (*resume_)(call_or_throw_op_base*) noexcept;
  bool is_throw_{false};
};

template <>
struct call_or_throw_op_base<true> {
  void call(accept_op_base_noargs& acceptor) noexcept;

  void (*unlocked_complete_)(call_or_throw_op_base*) noexcept {nullptr};
  void (*resume_)(call_or_throw_op_base*) noexcept;
};

template <bool Noexcept>
struct call_op_base : public call_or_throw_op_base<Noexcept> {
  void (*call_)(call_op_base*, accept_op_base_noargs&) noexcept(Noexcept);
};

struct throw_op_base : public call_or_throw_op_base<false> {
  explicit throw_op_base(std::exception_ptr ex) noexcept : ex_(std::move(ex)) {
    this->is_throw_ = true;
  }

  std::exception_ptr ex_;
};

inline void
call_or_throw_op_base<true>::call(accept_op_base_noargs& acceptor) noexcept {
  auto* self{static_cast<call_op_base<true>*>(this)};
  (*self->call_)(self, acceptor);
}

inline void
call_or_throw_op_base<false>::call(accept_op_base_noargs& acceptor) {
  if (is_throw_) {
    acceptor.rethrow(std::move(static_cast<throw_op_base*>(this)->ex_));
  } else {
    auto* self{static_cast<call_op_base<false>*>(this)};
    (*self->call_)(self, acceptor);
  }
}

struct sender_base {
  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = true;
  static constexpr blocking_kind blocking = blocking_kind::maybe;
  static constexpr bool is_always_scheduler_affine = true;

  sender_base() noexcept = default;
  sender_base(const sender_base&) = delete;
  sender_base(sender_base&&) = default;
};

// ===== Lock-free async_pass_base and operation types =====
//
// The async_pass_base uses a single tagged atomic pointer:
//   0              = idle
//   even, non-zero = call_or_throw_op_base<Noexcept>* (caller waiting)
//   odd            = accept_op_base_noargs* XOR 1     (acceptor waiting)
//
// A single atomic eliminates the TOCTOU race inherent in two-pointer
// designs: storing yourself and checking for a counterpart collapse
// into one CAS.
//
// The operation types (call_op, accept_op, throw_op) are NestedOps for
// cancellable<>. Each provides start() and stop(); cancellable<> handles
// stop token registration and try_complete coordination.

struct async_pass_base {
  std::atomic<uintptr_t> state_{0};

  static constexpr uintptr_t kAcceptorTag = 1;

  static bool is_caller(uintptr_t s) noexcept {
    return s != 0 && !(s & kAcceptorTag);
  }

  static bool is_acceptor(uintptr_t s) noexcept {
    return (s & kAcceptorTag) != 0;
  }

  static auto* as_acceptor(uintptr_t s) noexcept {
    static_assert(
        alignof(accept_op_base_noargs) >= 2,
        "acceptor base must be at least 2-byte aligned for pointer tagging");
    return reinterpret_cast<accept_op_base_noargs*>(s ^ kAcceptorTag);
  }

  template <bool Noexcept>
  static auto* as_caller(uintptr_t s) noexcept {
    static_assert(
        alignof(call_or_throw_op_base<Noexcept>) >= 2,
        "caller base must be at least 2-byte aligned for pointer tagging");
    return reinterpret_cast<call_or_throw_op_base<Noexcept>*>(s);
  }

  // --- Non-template CAS loops (defined in async_pass.cpp) ---

  accept_op_base_noargs* try_claim_acceptor() noexcept;
  uintptr_t try_claim_caller_raw() noexcept;
  uintptr_t call_or_suspend_raw(uintptr_t caller) noexcept;
  uintptr_t accept_or_suspend_raw(uintptr_t acceptor) noexcept;

  // --- Template wrappers (inline, trivial casts) ---

  template <bool Noexcept>
  call_or_throw_op_base<Noexcept>* try_claim_caller() noexcept {
    auto s = try_claim_caller_raw();
    return s ? as_caller<Noexcept>(s) : nullptr;
  }

  template <bool Noexcept>
  accept_op_base_noargs*
  call_or_suspend(call_or_throw_op_base<Noexcept>* caller) noexcept {
    auto s = call_or_suspend_raw(reinterpret_cast<uintptr_t>(caller));
    return s ? as_acceptor(s) : nullptr;
  }

  template <bool Noexcept>
  call_or_throw_op_base<Noexcept>*
  accept_or_suspend(accept_op_base_noargs* acceptor) noexcept {
    auto s = accept_or_suspend_raw(
        reinterpret_cast<uintptr_t>(acceptor) | kAcceptorTag);
    return s ? as_caller<Noexcept>(s) : nullptr;
  }
};

// --- call_op: NestedOp for cancellable<call_raw_sender> ---
//
// Inherits call_op_base<Noexcept> for the type-erased rendezvous interface
// (call_, resume_). When claimed by an acceptor, resume_ calls
// try_complete(this) and starts the completion_forwarder for scheduler
// affinity. stop() CAS-unclaims from async_pass_base; if successful,
// completes as cancelled via the same forwarder path.

template <
    bool Noexcept,
    typename CallerFn,
    typename ArgsList,
    typename Receiver>
class call_op;

template <bool Noexcept, typename CallerFn, typename... Args, typename Receiver>
class call_op<Noexcept, CallerFn, type_list<Args...>, Receiver>
  : public call_op_base<Noexcept> {
public:
  explicit call_op(
      async_pass_base& pass, CallerFn&& callerFn, Receiver&& receiver) noexcept
    : pass_(pass)
    , callerFn_(std::forward<CallerFn>(callerFn))
    , receiver_(std::forward<Receiver>(receiver)) {
    this->call_ = [](call_op_base<Noexcept>* base,
                     accept_op_base_noargs& acceptor) noexcept(Noexcept) {
      using acceptor_t =
          typename accept_op_base<Args...>::template callable<Noexcept>;
      std::move(static_cast<call_op*>(base)->callerFn_)(
          static_cast<acceptor_t&>(acceptor));
    };
    this->resume_ = [](call_or_throw_op_base<Noexcept>* base) noexcept {
      auto* self = static_cast<call_op*>(base);
      if (try_complete(self)) {
        self->forwardingOp_.start(*self);
      }
    };
  }

  void start() noexcept {
    if (auto* acceptor = pass_.call_or_suspend(this)) {
      if constexpr (Noexcept) {
        (*this->call_)(this, *acceptor);
      } else {
        UNIFEX_TRY {
          (*this->call_)(this, *acceptor);
        }
        UNIFEX_CATCH(...) {
          acceptor->rethrow(std::current_exception());
        }
      }
      (*acceptor->unlocked_complete_)(acceptor);
      (*this->resume_)(this);
    }
  }

  void stop() noexcept {
    auto expected = reinterpret_cast<uintptr_t>(
        static_cast<call_or_throw_op_base<Noexcept>*>(this));
    if (pass_.state_.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel)) {
      if (try_complete(this)) {
        cancelled_ = true;
        forwardingOp_.start(*this);
      }
    }
  }

  Receiver& get_receiver() noexcept { return receiver_; }

  void forward_set_value() noexcept {
    if (cancelled_) {
      unifex::set_done(std::move(receiver_));
    } else {
      unifex::set_value(std::move(receiver_));
    }
  }

private:
  async_pass_base& pass_;
  CallerFn callerFn_;
  Receiver receiver_;
  completion_forwarder<call_op, Receiver> forwardingOp_;
  bool cancelled_{false};
};

// --- throw_op: NestedOp for cancellable<throw_raw_sender> ---

template <typename Receiver>
class throw_op : public throw_op_base {
public:
  explicit throw_op(
      async_pass_base& pass,
      std::exception_ptr ex,
      Receiver&& receiver) noexcept
    : throw_op_base(std::move(ex))
    , pass_(pass)
    , receiver_(std::forward<Receiver>(receiver)) {
    this->resume_ = [](call_or_throw_op_base<false>* base) noexcept {
      auto* self = static_cast<throw_op*>(base);
      if (try_complete(self)) {
        self->forwardingOp_.start(*self);
      }
    };
  }

  void start() noexcept {
    if (auto* acceptor = pass_.call_or_suspend(this)) {
      (*acceptor->set_error_)(acceptor, std::move(this->ex_));
      (*acceptor->unlocked_complete_)(acceptor);
      (*this->resume_)(this);
    }
  }

  void stop() noexcept {
    auto expected = reinterpret_cast<uintptr_t>(
        static_cast<call_or_throw_op_base<false>*>(this));
    if (pass_.state_.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel)) {
      if (try_complete(this)) {
        cancelled_ = true;
        forwardingOp_.start(*this);
      }
    }
  }

  Receiver& get_receiver() noexcept { return receiver_; }

  void forward_set_value() noexcept {
    if (cancelled_) {
      unifex::set_done(std::move(receiver_));
    } else {
      unifex::set_value(std::move(receiver_));
    }
  }

private:
  async_pass_base& pass_;
  Receiver receiver_;
  completion_forwarder<throw_op, Receiver> forwardingOp_;
  bool cancelled_{false};
};

// --- accept_op: NestedOp for cancellable<accept_raw_sender> ---
//
// Inherits accept_op_base<Args...> for the type-erased acceptor interface
// (set_value_, set_error_). The unlocked_complete_ field (inherited from
// accept_op_base_noargs) serves as the resume function: when the caller
// side claims this acceptor and executes the call, it invokes
// unlocked_complete_ to trigger try_complete + completion_forwarder.
//
// Deferred completion storage (for value, error, or done) uses
// forwarding_state / manual_lifetime_union to capture the result
// during the rendezvous and deliver it after the scheduler hop.

template <bool Noexcept, typename Receiver, typename... Args>
class accept_op : public accept_op_base<Args...> {
  using op_base = accept_op_base<Args...>;

  template <typename FwdFn>
  struct forwarding_state {
    template <typename FwdFactory>
    explicit forwarding_state(FwdFactory&& fwdFactory) noexcept(Noexcept)
      : fwd_(fwdFactory()) {}

    static void
    deferred_complete(accept_op* self, Receiver&& receiver) noexcept {
      std::move(self->state_.template get<forwarding_state>().fwd_)(
          std::forward<Receiver>(receiver));
    }

    FwdFn fwd_;
  };

  static auto defer_set_value(Args&&... args) noexcept {
    return [&args...]() noexcept(Noexcept) {
      return [... args{std::decay_t<Args>{std::move(args)}}](
                 Receiver&& receiver) mutable noexcept(Noexcept) {
        unifex::set_value(std::forward<Receiver>(receiver), std::move(args)...);
      };
    };
  }

  static auto defer_set_error(std::exception_ptr ex) noexcept {
    return [ex{std::move(ex)}]() mutable noexcept {
      return [ex{std::move(ex)}](Receiver&& receiver) mutable noexcept {
        unifex::set_error(std::forward<Receiver>(receiver), std::move(ex));
      };
    };
  }

  static auto defer_set_done() noexcept {
    return []() noexcept {
      return [](Receiver&& receiver) noexcept {
        unifex::set_done(std::forward<Receiver>(receiver));
      };
    };
  }

public:
  explicit accept_op(async_pass_base& pass, Receiver&& receiver) noexcept
    : pass_(pass)
    , receiver_(std::forward<Receiver>(receiver)) {
    this->set_value_ = [](op_base* base, Args&&... args) noexcept {
      static_cast<accept_op*>(base)->locked_set_value(
          std::forward<Args>(args)...);
    };
    this->set_error_ = [](accept_op_base_noargs* base,
                          std::exception_ptr ex) noexcept {
      static_cast<accept_op*>(base)->locked_set_error(std::move(ex));
    };
    this->unlocked_complete_ = [](accept_op_base_noargs* base) noexcept {
      auto* self = static_cast<accept_op*>(base);
      if (try_complete(self)) {
        self->forwardingOp_.start(*self);
      }
    };
  }

  ~accept_op() noexcept { (*finalizer_)(this); }

  void start() noexcept {
    if (auto* caller = pass_.template accept_or_suspend<Noexcept>(this)) {
      if constexpr (Noexcept) {
        caller->call(*this);
      } else {
        UNIFEX_TRY {
          caller->call(*this);
        }
        UNIFEX_CATCH(...) {
          this->rethrow(std::current_exception());
        }
      }
      if (try_complete(this)) {
        forwardingOp_.start(*this);
      }
      (*caller->resume_)(caller);
    }
  }

  void stop() noexcept {
    auto expected =
        reinterpret_cast<uintptr_t>(static_cast<accept_op_base_noargs*>(this)) |
        async_pass_base::kAcceptorTag;
    if (pass_.state_.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel)) {
      locked_complete_with(defer_set_done());
      if (try_complete(this)) {
        forwardingOp_.start(*this);
      }
    }
  }

  Receiver& get_receiver() noexcept { return receiver_; }

  void forward_set_value() noexcept {
    if constexpr (Noexcept) {
      (*complete_)(this, std::move(receiver_));
    } else {
      UNIFEX_TRY {
        (*complete_)(this, std::move(receiver_));
      }
      UNIFEX_CATCH(...) {
        unifex::set_error(std::move(receiver_), std::current_exception());
      }
    }
  }

  void locked_set_value(Args&&... args) noexcept {
    if (complete_ == nullptr) {
      if constexpr (Noexcept) {
        locked_complete_with(defer_set_value(std::forward<Args>(args)...));
      } else {
        UNIFEX_TRY {
          locked_complete_with(defer_set_value(std::forward<Args>(args)...));
        }
        UNIFEX_CATCH(...) {
          locked_complete_with(defer_set_error(std::current_exception()));
        }
      }
    }
  }

  void locked_set_error(std::exception_ptr ex) noexcept {
    if (complete_ == nullptr) {
      locked_complete_with(defer_set_error(std::move(ex)));
    }
  }

  void locked_complete_with(auto&& deferred) noexcept(noexcept(deferred())) {
    using op_state_t =
        forwarding_state<std::remove_cvref_t<decltype(deferred())>>;

    complete_ = &op_state_t::deferred_complete;
    finalizer_ = [](accept_op* self) noexcept {
      deactivate_union_member<op_state_t>(self->state_);
    };
    activate_union_member_with<op_state_t>(
        state_, [&deferred]() noexcept(noexcept(deferred())) {
          return op_state_t{std::forward<decltype(deferred)>(deferred)};
        });
  }

  template <auto DeferFn, typename... Args2>
  using forwarding_state_t =
      forwarding_state<decltype(DeferFn(UNIFEX_DECLVAL(Args2)...)())>;

  async_pass_base& pass_;
  Receiver receiver_;
  completion_forwarder<accept_op, Receiver> forwardingOp_;
  manual_lifetime_union<
      forwarding_state_t<defer_set_value, Args&&...>,
      forwarding_state_t<defer_set_error, std::exception_ptr>,
      forwarding_state_t<defer_set_done>>
      state_;

  void (*complete_)(accept_op*, Receiver&&) noexcept {nullptr};
  void (*finalizer_)(accept_op*) noexcept = [](accept_op*) noexcept {
  };
};

// --- Raw sender types for cancellable<> ---

template <bool Noexcept, typename CallerFn, typename ArgsList>
class call_raw_sender : public sender_base {
public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  explicit call_raw_sender(
      async_pass_base& pass, CallerFn&& callerFn, ArgsList) noexcept
    : pass_(pass)
    , callerFn_(std::forward<CallerFn>(callerFn)) {}

  template(typename Receiver)                  //
      (requires scheduler_provider<Receiver>)  //
      friend auto tag_invoke(
          tag_t<connect>, call_raw_sender&& s, Receiver&& r) noexcept {
    return call_op<Noexcept, CallerFn, ArgsList, Receiver>{
        s.pass_, std::move(s.callerFn_), std::forward<Receiver>(r)};
  }

private:
  async_pass_base& pass_;
  CallerFn callerFn_;
};

class throw_raw_sender : public sender_base {
public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  explicit throw_raw_sender(
      async_pass_base& pass, std::exception_ptr ex) noexcept
    : pass_(pass)
    , ex_(std::move(ex)) {}

  template(typename Receiver)                  //
      (requires scheduler_provider<Receiver>)  //
      friend auto tag_invoke(
          tag_t<connect>, throw_raw_sender&& s, Receiver&& r) noexcept {
    return throw_op<Receiver>{
        s.pass_, std::move(s.ex_), std::forward<Receiver>(r)};
  }

private:
  async_pass_base& pass_;
  std::exception_ptr ex_;
};

template <bool Noexcept, typename... Args>
class accept_raw_sender : public sender_base {
public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<std::decay_t<Args>...>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  explicit accept_raw_sender(async_pass_base& pass) noexcept : pass_(pass) {}

  template(typename Receiver)                  //
      (requires scheduler_provider<Receiver>)  //
      friend auto tag_invoke(
          tag_t<connect>, accept_raw_sender&& s, Receiver&& r) noexcept {
    return accept_op<Noexcept, Receiver, Args...>{
        s.pass_, std::forward<Receiver>(r)};
  }

private:
  async_pass_base& pass_;
};

template(bool Noexcept, typename... Args)  //
    (requires constructible_v<Args...>
         AND(                                                 //
             !Noexcept || nothrow_constructible_v<Args...>))  //
    class async_pass : private async_pass_base {
private:
  struct acceptor_constraint {
    // For constraint checking only
    void operator()(auto&&... args) noexcept;
  };

public:
  bool is_idle() const noexcept {
    return this->state_.load(std::memory_order_acquire) == 0;
  }

  bool is_expecting_call() const noexcept {
    return async_pass_base::is_acceptor(
        this->state_.load(std::memory_order_acquire));
  }

  bool is_expecting_accept() const noexcept {
    return async_pass_base::is_caller(
        this->state_.load(std::memory_order_acquire));
  }

  template(typename F)  //
      (requires std::is_invocable_v<F, Args...>
           AND(                                                        //
               !Noexcept || std::is_nothrow_invocable_v<F, Args...>))  //
      bool try_accept(F&& f) noexcept(Noexcept) {
    if (auto* caller = this->template try_claim_caller<Noexcept>()) {
      auto acceptor{
          accept_call_with<Noexcept, type_list<Args...>>(std::forward<F>(f))};
      scope_guard resume{[caller]() noexcept { (*caller->resume_)(caller); }};
      if constexpr (Noexcept) {
        caller->call(acceptor);
      } else {
        UNIFEX_TRY {
          caller->call(acceptor);
        }
        UNIFEX_CATCH(...) {
          acceptor.rethrow(std::current_exception());
        }
      }
      return true;
    }
    return false;
  }

  [[nodiscard]] std::optional<std::tuple<std::decay_t<Args>...>>
  try_accept() noexcept(Noexcept) {
    std::optional<std::tuple<std::decay_t<Args>...>> result;
    try_accept([&result](Args&&... args) noexcept(Noexcept) {
      result.emplace(std::forward<Args>(args)...);
    });
    return result;
  }

  [[nodiscard]] auto async_accept() noexcept {
    return cancellable{accept_raw_sender<Noexcept, Args...>{*this}};
  }

  template(typename F)  //
      (requires std::is_invocable_v<F, acceptor_constraint&>
           AND(  //
               !Noexcept ||
               std::is_nothrow_invocable_v<F, acceptor_constraint&>))  //
      [[nodiscard]] bool try_call(F&& callerFn) noexcept {
    if (auto* acceptor = this->try_claim_acceptor()) {
      using acceptor_t =
          typename accept_op_base<Args...>::template callable<Noexcept>;
      if constexpr (Noexcept) {
        std::move(callerFn)(*static_cast<acceptor_t*>(acceptor));
      } else {
        UNIFEX_TRY {
          std::move(callerFn)(*static_cast<acceptor_t*>(acceptor));
        }
        UNIFEX_CATCH(...) {
          (*acceptor->set_error_)(acceptor, std::current_exception());
        }
      }
      (*acceptor->unlocked_complete_)(acceptor);
      return true;
    }
    return false;
  }

  [[nodiscard]] bool try_call(Args&&... args) noexcept {
    return try_call([&args...](auto& acceptorFn /* noexcept */) noexcept {
      acceptorFn(std::forward<Args>(args)...);
    });
  }

  template(bool Enable = !Noexcept)  //
      (requires Enable)              //
      [[nodiscard]] bool try_throw(const std::exception& ex) noexcept {
    return try_throw(std::make_exception_ptr(ex));
  }

  template(bool Enable = !Noexcept)  //
      (requires Enable)              //
      [[nodiscard]] bool try_throw(std::exception_ptr ex) noexcept {
    if (auto* acceptor = this->try_claim_acceptor()) {
      (*acceptor->set_error_)(acceptor, std::move(ex));
      (*acceptor->unlocked_complete_)(acceptor);
      return true;
    }
    return false;
  }

  template(typename F)  //
      (requires std::is_invocable_v<F, acceptor_constraint&>
           AND(  //
               !Noexcept ||
               std::is_nothrow_invocable_v<F, acceptor_constraint&>))  //
      [[nodiscard]] auto async_call(F&& callerFn) noexcept {
    return cancellable{call_raw_sender<Noexcept, F, type_list<Args...>>{
        *this, std::forward<F>(callerFn), type_list<Args...>{}}};
  }

  [[nodiscard]] auto async_call(auto&&... args) noexcept {
    return async_call([&args...](auto& acceptorFn) mutable noexcept(Noexcept) {
      acceptorFn(std::forward<Args>(args)...);
    });
  }

  template(bool Enable = !Noexcept)  //
      (requires Enable)              //
      [[nodiscard]] auto async_throw(const auto& ex) noexcept {
    return async_throw(std::make_exception_ptr(ex));
  }

  template(bool Enable = !Noexcept)  //
      (requires Enable)              //
      [[nodiscard]] auto async_throw(std::exception_ptr ex) noexcept {
    return cancellable{throw_raw_sender{*this, std::move(ex)}};
  }
};

}  // namespace _pass

template <typename... Args>
using async_pass = _pass::async_pass<false, Args...>;

template <typename... Args>
using nothrow_async_pass = _pass::async_pass<true, Args...>;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
