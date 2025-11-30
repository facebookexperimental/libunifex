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

#include <unifex/create_raw_sender.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime_union.hpp>

#include <unifex/detail/completion_forwarder.hpp>
#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _create_basic_sndr {

template <bool Valid, typename Factory, typename Arg>
struct _maybe_invoke_result;

template <typename Factory, typename Arg>
struct _maybe_invoke_result<true, Factory, Arg> {
  using type = std::invoke_result_t<Factory, Arg&>;
};

template <typename Factory>
struct _maybe_invoke_result<true, Factory, void> {
  using type = std::invoke_result_t<Factory>;
};

template <typename Factory, typename Arg>
struct _maybe_invoke_result<false, Factory, Arg> {
  using type = void;
};

template <typename Factory, typename Arg>
struct _factory_result {
  static constexpr bool valid = std::is_invocable_v<Factory, Arg&>;
  static constexpr bool nothrow = std::is_nothrow_invocable_v<Factory, Arg&>;
  using type = typename _maybe_invoke_result<valid, Factory, Arg>::type;
};

template <typename Factory>
struct _factory_result<Factory, void> {
  static constexpr bool valid = std::is_invocable_v<Factory>;
  static constexpr bool nothrow = std::is_nothrow_invocable_v<Factory>;
  using type = typename _maybe_invoke_result<valid, Factory, void>::type;
};

template <typename Factory, typename Arg, typename... Rest>
struct _first_valid_factory_result
  : public std::conditional_t<
        _factory_result<Factory, Arg>::valid,
        _factory_result<Factory, Arg>,
        _first_valid_factory_result<Factory, Rest...>> {
  static auto construct(Factory&& factory, Arg& arg, auto&... rest) noexcept(
      _factory_result<Factory, Arg>::valid
          ? _factory_result<Factory, Arg>::nothrow
          : _first_valid_factory_result<Factory, Rest...>::nothrow) {
    if constexpr (_factory_result<Factory, Arg>::valid) {
      return std::forward<Factory>(factory)(arg);
    } else {
      return _first_valid_factory_result<Factory, Rest...>::construct(
          std::forward<Factory>(factory), rest...);
    }
  }
};

template <typename Factory>
struct _first_valid_factory_result<Factory, void>
  : public _factory_result<Factory, void> {
  static auto construct(Factory&& factory) noexcept(
      _factory_result<Factory, void>::nothrow) {
    return std::forward<Factory>(factory)();
  }
};

template <typename Factory, typename... Args>
struct _valid_factory_result
  : _first_valid_factory_result<Factory, Args..., void> {
  static_assert(_valid_factory_result::valid, "Factory must be callable");
};

template <typename Factory, typename... Args>
using factory_result = typename _valid_factory_result<Factory, Args...>::type;

template <typename Factory, typename... Args>
constexpr bool nothrow_factory =
    _valid_factory_result<Factory, Args...>::nothrow;

template <typename Factory, typename... Args>
auto construct(Factory&& factory, Args&... args) noexcept(
    nothrow_factory<Factory, Args&...>) {
  return _valid_factory_result<Factory, Args&...>::construct(
      std::forward<Factory>(factory), args...);
}

struct _empty {
  struct factory {
    constexpr _empty operator()() const noexcept { return {}; }
  };
};

template <
    bool Forwarding,
    bool SendsDone,
    typename Receiver,
    typename... ValueTypes>
struct _receiver_wrapper;

template <typename Tr, typename Receiver, typename... ValueTypes>
using receiver_wrapper = _receiver_wrapper<
    Tr::is_always_scheduler_affine,
    Tr::sends_done,
    Receiver,
    ValueTypes...>;

template <typename Receiver, bool SendsDone, typename... ValueTypes>
struct _receiver_wrapper<false, SendsDone, Receiver, ValueTypes...> {
  explicit _receiver_wrapper(Receiver&& receiver) noexcept
    : receiver_(std::forward<Receiver>(receiver)) {}

  Receiver& get_receiver() noexcept { return receiver_; }

  void set_value(auto&&... ts) noexcept {
    UNIFEX_TRY {
      unifex::set_value(
          std::move(receiver_),
          static_cast<ValueTypes>(std::forward<decltype(ts)>(ts))...);
    }
    UNIFEX_CATCH(...) {
      unifex::set_error(std::move(receiver_), std::current_exception());
    }
  }

  void set_error(std::exception_ptr ex) noexcept {
    unifex::set_error(std::move(receiver_), ex);
  }

  void set_done() noexcept
    requires SendsDone
  {
    unifex::set_done(std::move(receiver_));
  }

  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
};

template <typename Receiver, bool SendsDone, typename... ValueTypes>
struct _receiver_wrapper<true, SendsDone, Receiver, ValueTypes...>
  : public _receiver_wrapper<false, SendsDone, Receiver, ValueTypes...> {
  using _receiver_wrapper<false, SendsDone, Receiver, ValueTypes...>::
      _receiver_wrapper;

  template <typename FwdFn>
  struct forwarding_state {
    template <typename FwdFactory>
    explicit forwarding_state(FwdFactory&& fwdFactory) : fwd_(fwdFactory()) {}

    static void
    deferred_complete(_receiver_wrapper* self, Receiver&& receiver) noexcept {
      std::move(self->state_.template get<forwarding_state>().fwd_)(
          std::forward<Receiver>(receiver));
    }

    FwdFn fwd_;
  };

  template <typename DeferFn, typename... Args>
  using forwarding_state_t = forwarding_state<
      std::invoke_result_t<std::invoke_result_t<DeferFn, Args...>>>;

  ~_receiver_wrapper() noexcept { (*finalizer_)(this); }

  void set_value(auto&&... ts) noexcept {
    UNIFEX_TRY {
      complete_with(defer_set_value(
          static_cast<ValueTypes>(std::forward<decltype(ts)>(ts))...));
    }
    UNIFEX_CATCH(...) {
      complete_with(defer_set_error(std::current_exception()));
    }
  }

  void set_error(std::exception_ptr ex) noexcept {
    complete_with(defer_set_error(ex));
  }

  void set_done() noexcept
    requires SendsDone
  {
    complete_with(defer_set_done());
  }

  void forward_set_value() noexcept {
    UNIFEX_TRY {
      (*complete_)(this, std::move(this->receiver_));
    }
    UNIFEX_CATCH(...) {
      unifex::set_error(std::move(this->receiver_), std::current_exception());
    }
  }

  static auto defer_set_value(ValueTypes&&... args) noexcept {
    return [&args...]() {
      return [... args{ValueTypes{std::move(args)}}](
                 Receiver&& receiver) mutable {
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

  static auto defer_set_done() noexcept
    requires SendsDone
  {
    return []() noexcept {
      return [](Receiver&& receiver) noexcept {
        unifex::set_done(std::forward<Receiver>(receiver));
      };
    };
  }

  void complete_with(auto&& deferred) noexcept(noexcept(deferred())) {
    using choice_t =
        forwarding_state<std::remove_cvref_t<decltype(deferred())>>;

    complete_ = &choice_t::deferred_complete;
    finalizer_ = [](_receiver_wrapper* self) noexcept {
      deactivate_union_member<choice_t>(self->state_);
    };
    activate_union_member_with<choice_t>(
        state_, [&deferred]() noexcept(noexcept(deferred())) {
          return choice_t{std::forward<decltype(deferred)>(deferred)};
        });

    fwd_.start(*this);
  }

  completion_forwarder<_receiver_wrapper, Receiver> fwd_;
  std::conditional_t<
      SendsDone,
      manual_lifetime_union<
          forwarding_state_t<decltype(defer_set_value), ValueTypes&&...>,
          forwarding_state_t<decltype(defer_set_error), std::exception_ptr>,
          forwarding_state_t<decltype(defer_set_done)>>,
      manual_lifetime_union<
          forwarding_state_t<decltype(defer_set_value), ValueTypes&&...>,
          forwarding_state_t<decltype(defer_set_error), std::exception_ptr>>>
      state_;
  void (*complete_)(_receiver_wrapper*, Receiver&&) noexcept = nullptr;
  void (*finalizer_)(_receiver_wrapper* self) noexcept =
      [](_receiver_wrapper* /*self*/) noexcept {
      };
};

enum class _event_type : uint8_t { start, callback, errback, stop };

template <_event_type Type>
struct _event {
  static constexpr bool is_start{Type == _event_type::start};
  static constexpr bool is_callback{Type == _event_type::callback};
  static constexpr bool is_errback{Type == _event_type::errback};
  static constexpr bool is_stop{Type == _event_type::stop};
};

struct _unsafe_cb_base {
  struct ptr {
    template <typename Op>
    Op* op() const noexcept {
      return reinterpret_cast<Op*>(ptr_);
    }

    operator bool() const noexcept { return ptr_ != nullptr; }

    void* ptr_;
  };

  template <typename Op>
  explicit _unsafe_cb_base(Op& op) noexcept : op_(&op) {}

  ptr get() const noexcept { return ptr{op_}; }

  void* opaque() const noexcept { return op_; }

  static ptr from_opaque(void* o) noexcept {
    UNIFEX_ASSERT(o != nullptr);
    return ptr{o};
  }

  void* op_{nullptr};
};

struct _safe_cb_base {
  using holder = std::shared_ptr<void*>;

  struct ptr {
    template <typename Op>
    Op* op() const noexcept {
      return *std::reinterpret_pointer_cast<Op*>(ptr_);
    }

    operator bool() const noexcept { return ptr_ != nullptr; }

    holder ptr_;
    _safe_cb_base* self{nullptr};
  };

  explicit _safe_cb_base(const holder& holder) noexcept : weak_(holder) {}

  ptr get() const noexcept { return ptr{weak_.lock()}; }

  void* opaque() noexcept { return this; }

  static ptr from_opaque(void* o) noexcept {
    UNIFEX_ASSERT(o != nullptr);
    auto* self{reinterpret_cast<_safe_cb_base*>(o)};
    return ptr{self->weak_.lock(), self};
  }

  std::weak_ptr<void*> weak_;
};

struct _do_nothing {};

template <typename Fallback, typename... Args>
class _opaque_safe_cb : private _safe_cb_base {
public:
  auto callback() const& noexcept { return callback_; }

  void* context() & noexcept { return opaque(); }

private:
  template <
      typename Op,
      typename Base,
      typename Event,
      typename F,
      typename... A>
  friend class _callback;

  using fallback_t = Fallback;

  explicit _opaque_safe_cb(
      const _safe_cb_base& base,
      Fallback&& fallback,
      void (*cb)(void*, Args...)) noexcept
    : _safe_cb_base(base)
    , callback_(cb)
    , fallback_(std::forward<Fallback>(fallback)) {}

  template <typename Op, typename Event>
  static void callback(void* o, Args... args);

  void (*callback_)(void*, Args...);
  UNIFEX_NO_UNIQUE_ADDRESS mutable Fallback fallback_;
};

template <typename Fallback, typename... Args>
template <typename Op, typename Event>
void _opaque_safe_cb<Fallback, Args...>::callback(void* o, Args... args) {
  static constexpr bool nothrow_body{noexcept(
      UNIFEX_DECLVAL(Op).body(UNIFEX_DECLVAL(Event), UNIFEX_DECLVAL(Args)...))};

  auto ptr{from_opaque(o)};

  if (ptr &&
      ptr.template op<Op>()->template callback_impl<nothrow_body, Event>(
          std::forward<Args>(args)...)) {
    return;
  }

  if constexpr (std::is_pointer_v<Fallback>) {
    UNIFEX_ASSERT(ptr.self != nullptr);
    (*static_cast<_opaque_safe_cb*>(ptr.self)->fallback_)(
        std::forward<Args>(args)...);
  } else if constexpr (!std::is_same_v<Fallback, _do_nothing>) {
    UNIFEX_ASSERT(ptr.self != nullptr);
    std::move(static_cast<_opaque_safe_cb*>(ptr.self)->fallback_)(
        std::forward<Args>(args)...);
  }
}

template <
    typename Op,
    typename Base,
    typename Event,
    typename Fallback,
    typename... Args>
class _callback : private Base {
  static constexpr bool nothrow_body{noexcept(
      UNIFEX_DECLVAL(Op).body(UNIFEX_DECLVAL(Event), UNIFEX_DECLVAL(Args)...))};

public:
  explicit _callback(auto& ref, Fallback&& fallback) noexcept
    : Base(ref)
    , fallback_(std::forward<Fallback>(fallback)) {}

  void operator()(Args... args) const noexcept {
    if (auto ptr = this->get()) {
      if (ptr.template op<Op>()->template callback_impl<nothrow_body, Event>(
              std::forward<Args>(args)...)) {
        return;
      }
    }

    if constexpr (!std::is_same_v<Fallback, _do_nothing>) {
      std::move(fallback_)(std::forward<Args>(args)...);
    }
  }

  std::pair<void*, void (*)(void*, Args...)> opaque() const noexcept
    requires std::is_same_v<Base, _unsafe_cb_base>
  {
    return {
        Base::opaque(), [](void* o, Args... args) {
          if (auto ptr = Base::from_opaque(o)) {
            ptr.template op<Op>()->template callback_impl<nothrow_body, Event>(
                std::forward<Args>(args)...);
          }
        }};
  }

  template <typename OpaqueCb = _opaque_safe_cb<Fallback, Args...>>
  auto opaque() const& noexcept
    requires std::is_same_v<Base, _safe_cb_base>
  {
    return OpaqueCb{
        *this,
        typename OpaqueCb::fallback_t{fallback_},
        &OpaqueCb::template callback<Op, Event>};
  }

  template <typename OpaqueCb = _opaque_safe_cb<Fallback, Args...>>
  auto opaque() && noexcept
    requires std::is_same_v<Base, _safe_cb_base>
  {
    return OpaqueCb{
        std::move(*this),
        typename OpaqueCb::fallback_t{std::move(fallback_)},
        &OpaqueCb::template callback<Op, Event>};
  }

private:
  UNIFEX_NO_UNIQUE_ADDRESS mutable Fallback fallback_;
};

template <
    typename Tr,
    typename Op,
    typename Receiver,
    bool Cancellable = Tr::sends_done>
struct _state;

template <typename Tr, typename Op, typename Receiver>
struct _state<Tr, Op, Receiver, false> {
  enum phase : uint8_t { starting, started, stopped_early, completed_normally };

  bool completed() const noexcept {
    return phase_ == stopped_early || phase_ == completed_normally;
  }

  bool not_started() const noexcept { return phase_ == starting; }

  void set_started() noexcept {
    switch (phase_) {
      case starting: phase_ = started; break;
      case stopped_early: break;
      default: std::terminate();
    }
  }

  void set_completed() noexcept {
    switch (phase_) {
      case starting: phase_ = stopped_early; break;
      case started: phase_ = completed_normally; break;
      default: std::terminate();
    }
  }

  phase phase_{starting};
};

template <typename Op>
struct _stop_callback {
  void operator()() const noexcept {
    auto guard{op_.lock()};
    if (op_.state_.completed()) {
      // Can safely ignore stop request after sender has completed
      return;
    }

    const _event<_event_type::stop> event{};

    if (op_.state_.not_started()) {
      op_.set_done();
    } else {
      if constexpr (Op::nothrow_on_stop) {
        op_.body(event);
      } else {
        UNIFEX_TRY {
          op_.body(event);
        }
        UNIFEX_CATCH(...) {
          op_.set_error(std::current_exception());
          return;
        }
      }
    }
  }

  Op& op_;
};

template <typename Tr, typename Op, typename Receiver>
struct _state<Tr, Op, Receiver, true> : _state<Tr, Op, Receiver, false> {
  using stop_callback_t = typename stop_token_type_t<
      Receiver>::template callback_type<_stop_callback<Op>>;

  ~_state() noexcept {
    if (this->phase_ == _state::started) {
      // Should not happen, but possible due to bugs in user code
      stop_.destruct();
    }
  }

  void set_completed() noexcept {
    if (this->phase_ != _state::starting) {
      stop_.destruct();
    }
    _state<Tr, Op, Receiver, false>::set_completed();
  }

  void set_started() noexcept {
    if (this->phase_ == _state::stopped_early) {
      stop_.destruct();
    }

    _state<Tr, Op, Receiver, false>::set_started();
  }

  manual_lifetime<stop_callback_t> stop_;
};

struct _lockable {
  struct factory {
    auto operator()(_lockable& state) const noexcept {
      return std::lock_guard{state.mutex_};
    }
  };

  std::mutex mutex_;
};

template <typename Tr, typename Op, typename Receiver>
struct _lockable_state
  : public _state<Tr, Op, Receiver>
  , public _lockable {};

template <
    typename Tr,
    typename Receiver,
    typename Body,
    typename CtxFactory,
    typename LockFactory,
    typename... ValueTypes>
class _op {
  using receiver_wrapper_t = receiver_wrapper<Tr, Receiver, ValueTypes...>;
  using context_t = factory_result<CtxFactory, Receiver&>;
  using state_t = std::conditional_t<
      std::is_same_v<LockFactory, _lockable::factory>,
      _lockable_state<Tr, _op, Receiver>,
      _state<Tr, _op, Receiver>>;

  static_assert(
      nothrow_factory<
          LockFactory,
          context_t&,
          _lockable_state<Tr, _op, Receiver>&>,
      "Lock factory must be noexcept");

  template <
      typename Op,
      typename Base,
      typename Event,
      typename Fallback,
      typename... Args>
  friend class _callback;

  template <typename Fallback, typename... Args>
  friend class _opaque_safe_cb;

  template <typename Op>
  friend struct _stop_callback;

public:
  _op(Receiver&& receiver, CtxFactory&& ctx_factory, LockFactory&& lock_factory, Body&& body) noexcept(
      std::is_nothrow_constructible_v<receiver_wrapper_t, Receiver> &&
      nothrow_factory<CtxFactory, Receiver&> &&
      std::is_nothrow_constructible_v<LockFactory, LockFactory&&> &&
      std::is_nothrow_constructible_v<Body, Body&&>)
    : receiver_(std::forward<Receiver>(receiver))
    , ctx_(construct(
          std::forward<CtxFactory>(ctx_factory), receiver_.get_receiver()))
    , lock_factory_(std::forward<LockFactory>(lock_factory))
    , body_(std::forward<Body>(body)) {}

  context_t& context() noexcept
    requires(!std::is_same_v<context_t, _empty>)
  {
    return ctx_;
  }

  template <typename... Ts>
    requires(convertible_to<Ts, ValueTypes> && ...)
  void set_value(Ts&&... args) noexcept {
    if (!state_.completed()) {
      state_.set_completed();
      receiver_.set_value(std::forward<decltype(args)>(args)...);
    }
  }

  void set_error(std::exception_ptr ex) noexcept {
    if (!state_.completed()) {
      state_.set_completed();
      receiver_.set_error(ex);
    }
  }

  template <typename Ex>
  void set_error(Ex&& ex) noexcept {
    set_error(std::make_exception_ptr(std::forward<Ex>(ex)));
  }

  void set_done() noexcept
    requires Tr::sends_done
  {
    if (!state_.completed()) {
      state_.set_completed();
      receiver_.set_done();
    }
  }

  template <typename... Args>
  friend auto unsafe_callback(_op& op) noexcept {
    using Event = _event<_event_type::callback>;
    return _callback<_op, _unsafe_cb_base, Event, _do_nothing, Args...>{op, {}};
  }

  template <typename... Args>
  friend auto unsafe_errback(_op& op) noexcept {
    using Event = _event<_event_type::errback>;
    return _callback<_op, _unsafe_cb_base, Event, _do_nothing, Args...>{op, {}};
  }

  template <typename... Args>
  friend auto safe_callback(_op& op) noexcept {
    using Event = _event<_event_type::callback>;
    return _callback<_op, _safe_cb_base, Event, _do_nothing, Args...>{
        op.safe_cb_holder(), {}};
  }

  template <typename... Args>
  friend auto safe_callback(_op& op, auto&& fallback) noexcept
    requires std::is_invocable_v<decltype(fallback), Args...>
  {
    using Fallback = std::decay_t<decltype(fallback)>;
    using Event = _event<_event_type::callback>;
    return _callback<_op, _safe_cb_base, Event, Fallback, Args...>{
        op.safe_cb_holder(), std::forward<Fallback>(fallback)};
  }

  template <typename... Args>
  friend auto safe_errback(_op& op) noexcept {
    using Event = _event<_event_type::errback>;
    return _callback<_op, _safe_cb_base, Event, _do_nothing, Args...>{
        op.safe_cb_holder(), {}};
  }

  template <typename... Args>
  friend auto safe_errback(_op& op, auto&& fallback) noexcept
    requires std::is_invocable_v<decltype(fallback), Args...>
  {
    using Fallback = std::decay_t<decltype(fallback)>;
    using Event = _event<_event_type::errback>;
    return _callback<_op, _safe_cb_base, Event, Fallback, Args...>{
        op.safe_cb_holder(), std::forward<Fallback>(fallback)};
  }

private:
  template <typename Event, typename... Args>
  void body(Event evt, Args&&... args) noexcept(
      noexcept(body_(evt, *this, std::forward<Args>(args)...)))
    requires std::is_invocable_v<Body, Event, _op&, Args...>
  {
    body_(evt, *this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void body(_event<_event_type::start> /* evt */, Args&&... args) noexcept(
      noexcept(body_.start(*this, std::forward<Args>(args)...)))
    requires(
        !std::is_invocable_v<Body, _event<_event_type::start>, _op&, Args...>)
  {
    body_.start(*this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void body(_event<_event_type::callback> /* evt */, Args&&... args) noexcept(
      noexcept(body_.callback(*this, std::forward<Args>(args)...)))
    requires(
        !std::
            is_invocable_v<Body, _event<_event_type::callback>, _op&, Args...>)
  {
    body_.callback(*this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void body(_event<_event_type::errback> /* evt */, Args&&... args) noexcept(
      noexcept(body_.errback(*this, std::forward<Args>(args)...)))
    requires(
        !std::is_invocable_v<Body, _event<_event_type::errback>, _op&, Args...>)
  {
    body_.errback(*this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void body(_event<_event_type::stop> /* evt */, Args&&... args) noexcept(
      noexcept(body_.stop(*this, std::forward<Args>(args)...)))
    requires(
        !std::is_invocable_v<Body, _event<_event_type::stop>, _op&, Args...>)
  {
    body_.stop(*this, std::forward<Args>(args)...);
  }

  static constexpr bool nothrow_on_start{
      noexcept(UNIFEX_DECLVAL(_op).body(_event<_event_type::start>{}))};

  static constexpr bool nothrow_on_stop{
      !Tr::sends_done ||
      noexcept(UNIFEX_DECLVAL(_op).body(_event<_event_type::stop>{}))};

  friend void tag_invoke(tag_t<start>, _op& self) noexcept {
    if constexpr (nothrow_on_start) {
      self.start_impl();
    } else {
      UNIFEX_TRY {
        self.start_impl();
      }
      UNIFEX_CATCH(...) {
        auto guard{self.lock()};
        self.set_error(std::current_exception());
      }
    }
  }

  auto lock() noexcept { return construct(lock_factory_, ctx_, state_); }

  void start_impl() noexcept(nothrow_on_start) {
    if constexpr (Tr::sends_done) {
      state_.stop_.construct(
          get_stop_token(receiver_.get_receiver()), _stop_callback<_op>{*this});
    }

    auto guard{lock()};
    state_.set_started();

    if (state_.completed()) {
      // Stopped before start
      return;
    }

    body(_event<_event_type::start>{});
  }

  template <bool Noexcept, typename Event, typename... Args>
  bool callback_impl(Args&&... args) noexcept {
    auto guard{lock()};
    if (state_.completed()) {
      return false;
    }

    if constexpr (Noexcept) {
      body(Event{}, std::forward<Args>(args)...);
    } else {
      UNIFEX_TRY {
        body(Event{}, std::forward<Args>(args)...);
      }
      UNIFEX_CATCH(...) {
        set_error(std::current_exception());
      }
    }

    return true;
  }

  const _safe_cb_base::holder& safe_cb_holder() noexcept {
    if (safe_cb_holder_ == nullptr) {
      safe_cb_holder_ = std::make_shared<void*>(this);
    }
    return safe_cb_holder_;
  }

  _safe_cb_base::holder safe_cb_holder_{nullptr};
  state_t state_;

  UNIFEX_NO_UNIQUE_ADDRESS receiver_wrapper_t receiver_;
  UNIFEX_NO_UNIQUE_ADDRESS context_t ctx_;
  UNIFEX_NO_UNIQUE_ADDRESS LockFactory lock_factory_;
  UNIFEX_NO_UNIQUE_ADDRESS Body body_;
};

template <typename... ValueTypes>
class _fn {
public:
  template <
      typename Body,
      typename Tr =
          decltype(with_sender_traits<_make_traits::sender_traits_literal{}>)>
    requires move_constructible<Body> && _make_traits::is_traits_type_v<Tr>
  auto operator()(Body && body, Tr = {}) const
      noexcept(std::is_nothrow_constructible_v<Body, Body&&>) {
    return create_sender(
        std::forward<Body>(body),
        _empty::factory{},
        _lockable::factory{},
        Tr{});
  }

  template <
      typename Body,
      typename CtxFactory,
      typename Tr =
          decltype(with_sender_traits<_make_traits::sender_traits_literal{}>)>
    requires move_constructible<Body> && move_constructible<CtxFactory> &&
      (!_make_traits::is_traits_type_v<std::decay_t<CtxFactory>>) &&
      _make_traits::is_traits_type_v<Tr>
  auto operator()(Body && body, CtxFactory && ctxFactory, Tr = {}) const
      noexcept(
          std::is_nothrow_constructible_v<Body, Body&&> &&
          std::is_nothrow_constructible_v<CtxFactory, CtxFactory&&>) {
    return create_sender(
        std::forward<Body>(body),
        std::forward<CtxFactory>(ctxFactory),
        _lockable::factory{},
        Tr{});
  }

  template <
      typename Body,
      typename CtxFactory,
      typename LockFactory,
      typename Tr =
          decltype(with_sender_traits<_make_traits::sender_traits_literal{}>)>
    requires move_constructible<Body> && move_constructible<CtxFactory> &&
      move_constructible<LockFactory> &&
      (!_make_traits::is_traits_type_v<std::decay_t<LockFactory>>) &&
      _make_traits::is_traits_type_v<Tr>
  auto operator()(
      Body && body,
      CtxFactory &&
          ctxFactory, LockFactory && lockFactory, Tr = {}) const
      noexcept(
          std::is_nothrow_constructible_v<Body, Body&&> &&
          std::is_nothrow_constructible_v<CtxFactory, CtxFactory&&> &&
          std::is_nothrow_constructible_v<LockFactory, LockFactory&&>) {
    return create_sender(
        std::forward<Body>(body),
        std::forward<CtxFactory>(ctxFactory),
        std::forward<LockFactory>(lockFactory),
        Tr{});
  }

private:
  template <
      typename Body,
      typename CtxFactory,
      typename LockFactory,
      typename Tr>
  static auto
  create_sender(Body&& body, CtxFactory&& ctxFactory, LockFactory&& lockFactory, Tr) noexcept(
      std::is_nothrow_constructible_v<Body, Body&&> &&
      std::is_nothrow_constructible_v<CtxFactory, CtxFactory&&> &&
      std::is_nothrow_constructible_v<LockFactory, LockFactory&&>) {
    return create_raw_sender<ValueTypes...>(
        [body{std::forward<Body>(body)},
         ctxFactory{std::forward<CtxFactory>(ctxFactory)},
         lockFactory{std::forward<LockFactory>(lockFactory)}](
            auto&& receiver) mutable noexcept(std::
                                                  is_nothrow_constructible_v<
                                                      _op<Tr,
                                                          decltype(receiver),
                                                          Body,
                                                          CtxFactory,
                                                          LockFactory,
                                                          ValueTypes...>>) {
          return _op<
              Tr,
              std::decay_t<decltype(receiver)>,
              Body,
              CtxFactory,
              LockFactory,
              ValueTypes...>{
              std::forward<decltype(receiver)>(receiver),
              std::move(ctxFactory),
              std::move(lockFactory),
              std::move(body)};
        },
        Tr{});
  }
};

}  // namespace _create_basic_sndr

template <typename... ValueTypes>
inline constexpr _create_basic_sndr::_fn<ValueTypes...> create_basic_sender{};

template <typename... Args>
using basic_sender_opaque_callback = _create_basic_sndr::
    _opaque_safe_cb<_create_basic_sndr::_do_nothing, Args...>;

template <typename Fallback, typename... Args>
using basic_sender_opaque_callback_with_fallback =
    _create_basic_sndr::_opaque_safe_cb<Fallback, Args...>;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
