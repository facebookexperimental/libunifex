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

#include <unifex/detail/completion_forwarder.hpp>
#include <unifex/detail/concept_macros.hpp>

#include <mutex>
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
    explicit op(AcceptorFn&& acceptorFn, std::false_type) noexcept
      : acceptorFn_(std::forward<AcceptorFn>(acceptorFn)) {
      this->set_value_ = [](accept_op_base<Args...>* self, Args&&... args) {
        static_cast<op*>(self)->acceptorFn_(std::forward<Args>(args)...);
      };
      this->set_error_ = [](accept_op_base_noargs* self,
                            std::exception_ptr ex) {
        std::rethrow_exception(std::move(ex));
      };
    }

    explicit op(AcceptorFn&& acceptorFn, std::true_type) noexcept
      : acceptorFn_(std::forward<AcceptorFn>(acceptorFn)) {
      this->set_value_ = [](accept_op_base<Args...>* self,
                            Args&&... args) noexcept {
        static_cast<op*>(self)->acceptorFn_(std::forward<Args>(args)...);
      };
    }

    AcceptorFn acceptorFn_;
  };

  template <typename ArgsList>
  using type = apply_to_type_list_t<op, ArgsList>;
};

template <bool Noexcept, typename ArgsList, typename AcceptorFn>
auto accept_call_with(AcceptorFn&& acceptorFn) {
  return typename immediate_accept<AcceptorFn>::template type<ArgsList>{
      std::forward<AcceptorFn>(acceptorFn),
      std::integral_constant<bool, Noexcept>{}};
}

template <bool Noexcept>
struct call_or_throw_op_base;

template <>
struct call_or_throw_op_base<false> {
  void call(accept_op_base_noargs& acceptor);

  void (*resume_)(call_or_throw_op_base*) noexcept;
  bool is_throw_{false};
};

template <>
struct call_or_throw_op_base<true> {
  void call(accept_op_base_noargs& acceptor) noexcept;

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
  (*resume_)(this);
}

inline void
call_or_throw_op_base<false>::call(accept_op_base_noargs& acceptor) {
  scope_guard guard{[this]() noexcept {
    (*resume_)(this);
  }};
  if (is_throw_) {
    acceptor.rethrow(std::move(static_cast<throw_op_base*>(this)->ex_));
  } else {
    auto* self{static_cast<call_op_base<false>*>(this)};
    (*self->call_)(self, acceptor);
  }
}

template <bool Noexcept>
struct async_pass_base {
  bool locked_try_throw(std::exception_ptr ex) noexcept {
    UNIFEX_ASSERT(this->waiting_call_ == nullptr);
    if (auto* accept{std::exchange(waiting_accept_, nullptr)}) {
      (*accept->set_error_)(accept, std::move(ex));
      return true;
    } else {
      return false;
    }
  }

  template <typename ArgsList, typename CallerFn>
  bool locked_try_call(CallerFn&& callerFn) noexcept {
    UNIFEX_ASSERT(this->waiting_call_ == nullptr);
    if (auto* accept{std::exchange(waiting_accept_, nullptr)}) {
      using op_base = typename apply_to_type_list_t<accept_op_base, ArgsList>::
          template callable<Noexcept>;
      if constexpr (Noexcept) {
        std::move(callerFn)(*static_cast<op_base*>(accept));
      } else {
        UNIFEX_TRY {
          std::move(callerFn)(*static_cast<op_base*>(accept));
        }
        UNIFEX_CATCH(...) {
          (*accept->set_error_)(accept, std::current_exception());
        }
      }
      return true;
    } else {
      return false;
    }
  }

  bool locked_try_accept(accept_op_base_noargs& acceptor) noexcept(Noexcept) {
    UNIFEX_ASSERT(this->waiting_accept_ == nullptr);
    if (auto* call_or_throw{std::exchange(this->waiting_call_, nullptr)}) {
      call_or_throw->call(acceptor);
      return true;
    } else {
      return false;
    }
  }

  bool nothrow_locked_try_accept(accept_op_base_noargs& acceptor) noexcept {
    UNIFEX_ASSERT(this->waiting_accept_ == nullptr);
    if constexpr (Noexcept) {
      return locked_try_accept(acceptor);
    } else if (auto* call_or_throw{
                   std::exchange(this->waiting_call_, nullptr)}) {
      UNIFEX_TRY {
        call_or_throw->call(acceptor);
      }
      UNIFEX_CATCH(...) {
        acceptor.rethrow(std::current_exception());
      }
      return true;
    } else {
      return false;
    }
  }

  mutable std::mutex mutex_;
  call_or_throw_op_base<Noexcept>* waiting_call_{nullptr};
  accept_op_base_noargs* waiting_accept_{nullptr};
};

template <bool Noexcept, typename Receiver>
class call_or_throw_op_impl {
protected:
  enum CompletionState : uint8_t { kNotCompleted, kCompleted, kCancelled };

public:
  Receiver& get_receiver() noexcept { return receiver_; }

  void forward_set_value() noexcept {
    switch (completion_) {
      case kCompleted: unifex::set_value(std::move(receiver_)); break;
      case kCancelled: unifex::set_done(std::move(receiver_)); break;
      case kNotCompleted:
      default: std::terminate();
    }
  }

protected:
  struct stop_callback final {
    void operator()() noexcept { self->set_done(); }

    call_or_throw_op_impl* self;
  };

  using stop_callback_type =
      stop_token_type_t<Receiver>::template callback_type<stop_callback>;

  explicit call_or_throw_op_impl(async_pass_base<Noexcept>& pass, Receiver&& r)
    : pass_(pass)
    , receiver_(std::forward<Receiver>(r)) {}

  void set_done() noexcept {
    std::lock_guard lock{pass_.mutex_};
    locked_complete(kCancelled);
  }

  void locked_complete(CompletionState completion) noexcept {
    if (completion_ == kNotCompleted) {
      completion_ = completion;
      pass_.waiting_call_ = nullptr;
      stop_callback_.reset();
      forwardingOp_.start(*this);
    }
  }

  async_pass_base<Noexcept>& pass_;
  Receiver receiver_;
  completion_forwarder<call_or_throw_op_impl, Receiver> forwardingOp_;
  std::optional<stop_callback_type> stop_callback_;
  CompletionState completion_{kNotCompleted};
};

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

template <bool Noexcept, typename CallerFn, typename ArgsList>
class call_sender : public sender_base {
  template <typename Receiver>
  struct _op;

public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<>>;

  template <typename Receiver>
  using operation = typename _op<Receiver>::type;

  template(typename Receiver)                                            //
      (requires receiver_of<Receiver> AND scheduler_provider<Receiver>)  //
      friend operation<Receiver> tag_invoke(
          tag_t<connect>, call_sender&& s, Receiver&& r) noexcept {
    return operation<Receiver>{
        s.pass_, std::move(s.callerFn_), std::forward<Receiver>(r)};
  }

  explicit call_sender(
      async_pass_base<Noexcept>& pass, CallerFn&& callerFn, ArgsList) noexcept
    : pass_(pass)
    , callerFn_{std::forward<CallerFn>(callerFn)} {}

private:
  template <typename Receiver>
  struct _op {
    class type
      : public call_op_base<Noexcept>
      , public call_or_throw_op_impl<Noexcept, Receiver> {
      using impl = call_or_throw_op_impl<Noexcept, Receiver>;

    public:
      explicit type(
          async_pass_base<Noexcept>& pass,
          CallerFn&& callerFn,
          Receiver&& r) noexcept
        : call_or_throw_op_impl<Noexcept, Receiver>(
              pass, std::forward<Receiver>(r))
        , callerFn_(std::forward<CallerFn>(callerFn)) {
        this->call_ = [](call_op_base<Noexcept>* self,
                         accept_op_base_noargs& acceptor) noexcept(Noexcept) {
          using acceptor_t =
              typename apply_to_type_list_t<accept_op_base, ArgsList>::
                  template callable<Noexcept>;
          std::move(static_cast<type*>(self)->callerFn_)(
              static_cast<acceptor_t&>(acceptor));
        };
        this->resume_ = [](call_or_throw_op_base<Noexcept>* self) noexcept {
          static_cast<type*>(self)->locked_complete(impl::kCompleted);
        };
      }

      void start() noexcept {
        impl::stop_callback_.emplace(
            get_stop_token(impl::receiver_),
            typename impl::stop_callback{this});
        std::lock_guard lock{this->pass_.mutex_};
        if (impl::stop_callback_
                .has_value()) {  // Else it fired on registration
          if (!this->pass_.template locked_try_call<ArgsList>(
                  std::move(callerFn_))) {
            this->pass_.waiting_call_ = this;
          } else {
            this->locked_complete(impl::kCompleted);
          }
        }
      }

    private:
      CallerFn callerFn_;
    };
  };

  async_pass_base<Noexcept>& pass_;
  CallerFn callerFn_;
};

class throw_sender : public sender_base {
  template <typename Receiver>
  struct _op;

public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<>>;

  template <typename Receiver>
  using operation = typename _op<Receiver>::type;

  template(typename Receiver)                                            //
      (requires receiver_of<Receiver> AND scheduler_provider<Receiver>)  //
      friend operation<Receiver> tag_invoke(
          tag_t<connect>, throw_sender&& s, Receiver&& r) noexcept {
    return operation<Receiver>{
        s.pass_, std::move(s.ex_), std::forward<Receiver>(r)};
  }

  explicit throw_sender(
      async_pass_base<false>& pass, std::exception_ptr ex) noexcept
    : pass_(pass)
    , ex_{ex} {}

private:
  template <typename Receiver>
  struct _op {
    class type
      : public throw_op_base
      , public call_or_throw_op_impl<false, Receiver> {
      using impl = call_or_throw_op_impl<false, Receiver>;

    public:
      explicit type(
          async_pass_base<false>& pass,
          std::exception_ptr ex,
          Receiver&& r) noexcept
        : throw_op_base(std::move(ex))
        , call_or_throw_op_impl<false, Receiver>(
              pass, std::forward<Receiver>(r)) {
        this->ex_ = std::move(ex);
        this->resume_ = [](call_or_throw_op_base<false>* self) noexcept {
          static_cast<type*>(self)->locked_complete(impl::kCompleted);
        };
      }

      void start() noexcept {
        impl::stop_callback_.emplace(
            get_stop_token(impl::receiver_),
            typename impl::stop_callback{this});
        std::lock_guard lock{this->pass_.mutex_};
        if (impl::stop_callback_
                .has_value()) {  // Else it fired on registration
          if (!this->pass_.locked_try_throw(std::move(this->ex_))) {
            this->pass_.waiting_call_ = this;
          } else {
            this->locked_complete(impl::kCompleted);
          }
        }
      }
    };
  };

  async_pass_base<false>& pass_;
  std::exception_ptr ex_;
};

template(bool Noexcept, typename... Args)                //
    (requires constructible_v<Args...> AND(              //
        !Noexcept || nothrow_constructible_v<Args...>))  //
    class async_pass : private async_pass_base<Noexcept> {
private:
  class accept_sender;

  struct acceptor_constraint {
    // For constraint checking only
    void operator()(Args&&... args) noexcept;
  };

public:
  bool is_idle() const noexcept {
    std::lock_guard lock{this->mutex_};
    return this->waiting_call_ == nullptr && this->waiting_accept_ == nullptr;
  }

  bool is_expecting_call() const noexcept {
    std::lock_guard lock{this->mutex_};
    return this->waiting_accept_ != nullptr;
  }

  bool is_expecting_accept() const noexcept {
    std::lock_guard lock{this->mutex_};
    return this->waiting_call_ != nullptr;
  }

  template(typename F)                                            //
      (requires std::is_invocable_v<F, Args...> AND(              //
          !Noexcept || std::is_nothrow_invocable_v<F, Args...>))  //
      bool try_accept(F&& f) noexcept(Noexcept) {
    std::lock_guard lock{this->mutex_};
    auto acceptor{
        accept_call_with<Noexcept, type_list<Args...>>(std::forward<F>(f))};
    return this->locked_try_accept(acceptor);
  }

  [[nodiscard]] std::optional<std::tuple<std::decay_t<Args>...>>
  try_accept() noexcept(Noexcept) {
    std::optional<std::tuple<std::decay_t<Args>...>> result;
    try_accept([&result](Args&&... args) noexcept(Noexcept) {
      result.emplace(std::forward<Args>(args)...);
    });
    return result;
  }

  [[nodiscard]] auto async_accept() noexcept { return accept_sender{*this}; }

  template(typename F)                                             //
      (requires std::is_invocable_v<F, acceptor_constraint&> AND(  //
          !Noexcept ||
          std::is_nothrow_invocable_v<F, acceptor_constraint&>))  //
      [[nodiscard]] bool try_call(F&& callerFn) noexcept {
    std::lock_guard lock{this->mutex_};
    return this->template locked_try_call<type_list<Args...>>(
        std::forward<F>(callerFn));
  }

  [[nodicard]] bool try_call(Args&&... args) noexcept {
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
    std::lock_guard lock{this->mutex_};
    return this->locked_try_throw(std::move(ex));
  }

  template(typename F)                                             //
      (requires std::is_invocable_v<F, acceptor_constraint&> AND(  //
          !Noexcept ||
          std::is_nothrow_invocable_v<F, acceptor_constraint&>))  //
      [[nodiscard]] auto async_call(F&& callerFn) noexcept {
    return call_sender{*this, std::forward<F>(callerFn), type_list<Args...>{}};
  }

  [[nodiscard]] auto async_call(Args&&... args) noexcept {
    return async_call([&args...](auto& acceptorFn) mutable noexcept(Noexcept) {
      acceptorFn(std::forward<Args>(args)...);
    });
  }

  template(bool Enable = !Noexcept)  //
      (requires Enable)              //
      [[nodiscard]] throw_sender async_throw(const auto& ex) noexcept {
    return async_throw(std::make_exception_ptr(ex));
  }

  template(bool Enable = !Noexcept)  //
      (requires Enable)              //
      [[nodiscard]] throw_sender async_throw(std::exception_ptr ex) noexcept {
    return throw_sender{*this, std::move(ex)};
  }

private:
  class accept_sender : public sender_base {
    friend async_pass;

    template <typename Receiver>
    struct _op;

  public:
    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types = Variant<Tuple<std::decay_t<Args>...>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    template <typename Receiver>
    using operation = typename _op<Receiver>::type;

    template(typename Receiver)                  //
        (requires scheduler_provider<Receiver>)  //
        friend operation<Receiver> tag_invoke(
            tag_t<connect>, accept_sender&& s, Receiver&& r) noexcept {
      return operation<Receiver>{s.pass_, std::forward<Receiver>(r)};
    }

  private:
    using op_base = accept_op_base<Args...>;

    template <typename Receiver>
    struct _op {
      class type;
      using stop_token_t = stop_token_type_t<Receiver>;

      class type : public op_base {
      private:
        friend accept_sender;

        struct stop_callback final {
          void operator()() noexcept { self->set_done(); }

          type* self;
        };

        using stop_callback_type =
            typename stop_token_t::template callback_type<stop_callback>;

        template <typename FwdFn>
        struct forwarding_state {
          template <typename FwdFactory>
          explicit forwarding_state(FwdFactory&& fwdFactory) noexcept(Noexcept)
            : fwd_(fwdFactory()) {}

          static void
          deferred_complete(type* self, Receiver&& receiver) noexcept {
            std::move(self->state_.template get<forwarding_state>().fwd_)(
                std::forward<Receiver>(receiver));
          }

          FwdFn fwd_;
        };

        static auto defer_set_value(Args&&... args) noexcept {
          return [&args...]() noexcept(Noexcept) {
            return [... args{std::decay_t<Args>{std::move(args)}}](
                       Receiver&& receiver) mutable noexcept(Noexcept) {
              unifex::set_value(
                  std::forward<Receiver>(receiver), std::move(args)...);
            };
          };
        }

        static auto defer_set_error(std::exception_ptr ex) noexcept {
          return [&ex]() noexcept {
            return [ex{std::move(ex)}](Receiver&& receiver) mutable noexcept {
              unifex::set_error(
                  std::forward<Receiver>(receiver), std::move(ex));
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
        explicit type(async_pass& pass, Receiver&& r) noexcept
          : pass_(pass)
          , receiver_(std::forward<Receiver>(r)) {
          this->set_value_ = [](op_base* self, Args&&... args) noexcept {
            static_cast<type*>(self)->locked_set_value(
                std::forward<Args>(args)...);
          };
          this->set_error_ = [](accept_op_base_noargs* self,
                                std::exception_ptr ex) noexcept {
            static_cast<type*>(self)->locked_set_error(std::move(ex));
          };
        }

        ~type() noexcept { (*this->finalizer_)(this); }

        type(type&&) = delete;

        void start() noexcept {
          stop_callback_.emplace(
              get_stop_token(receiver_), stop_callback{this});
          std::lock_guard lock{pass_.mutex_};
          if (stop_callback_.has_value()) {  // Else it fired on registration
            if (!pass_.nothrow_locked_try_accept(*this)) {
              pass_.waiting_accept_ = this;
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

      private:
        void set_done() noexcept {
          std::lock_guard lock{pass_.mutex_};
          if (complete_ == nullptr) {
            locked_complete_with(defer_set_done());
          }
        }

        void locked_set_value(Args&&... args) noexcept {
          if (complete_ == nullptr) {
            if constexpr (Noexcept) {
              locked_complete_with(
                  defer_set_value(std::forward<Args>(args)...));
            } else {
              UNIFEX_TRY {
                locked_complete_with(
                    defer_set_value(std::forward<Args>(args)...));
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

        void
        locked_complete_with(auto&& deferred) noexcept(noexcept(deferred())) {
          using op_state_t =
              forwarding_state<std::remove_cvref_t<decltype(deferred())>>;

          stop_callback_.reset();
          pass_.waiting_accept_ = nullptr;
          complete_ = &op_state_t::deferred_complete;
          finalizer_ = [](type* self) noexcept {
            deactivate_union_member<op_state_t>(self->state_);
          };
          activate_union_member_with<op_state_t>(
              state_, [&deferred]() noexcept(noexcept(deferred())) {
                return op_state_t{std::forward<decltype(deferred)>(deferred)};
              });

          forwardingOp_.start(*this);
        }

        template <typename DeferFn, typename... Args2>
        using forwarding_state_t = forwarding_state<
            std::invoke_result_t<std::invoke_result_t<DeferFn, Args2...>>>;

        async_pass& pass_;
        Receiver receiver_;
        completion_forwarder<type, Receiver> forwardingOp_;
        manual_lifetime_union<
            forwarding_state_t<decltype(defer_set_value), Args&&...>,
            forwarding_state_t<decltype(defer_set_error), std::exception_ptr>,
            forwarding_state_t<decltype(defer_set_done)>>
            state_;
        std::optional<stop_callback_type> stop_callback_;

        void (*complete_)(type*, Receiver&&) noexcept = nullptr;
        void (*finalizer_)(type* self) noexcept = [](type* /*self*/) noexcept {
        };
      };
    };

    explicit accept_sender(async_pass& pass) noexcept : pass_(pass) {}

    async_pass& pass_;
  };

  friend accept_sender;
  friend throw_sender;

  template <bool Noex, typename F, typename ArgsList>
  friend class call_sender;
};

}  // namespace _pass

template <typename... Args>
using async_pass = _pass::async_pass<false, Args...>;

template <typename... Args>
using nothrow_async_pass = _pass::async_pass<true, Args...>;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
