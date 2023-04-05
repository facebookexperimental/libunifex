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

#include <unifex/async_manual_reset_event.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_allocator.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/let_done.hpp>
#include <unifex/let_value.hpp>
#include <unifex/let_value_with.hpp>
#include <unifex/let_value_with_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/nest.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/variant_sender.hpp>

#include <exception>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _spawn_future {

// Spawning a future creates a race condition between the spawned operation
// completing and the future trying to observe the result; we resolve this race
// by moving the operation through a state machine that reflects what work has
// been done and who's waiting for whom.
//
// Successfully-spawned operations start in the init state; from there, the
// simplest path is for the operation to complete normally before the future's
// continuation consumes the result, in which case the operation moves from init
// to one of value, error, or done (depending on the result) and then the
// future deletes the spawned operation as a side effect of completing.
//
// There are several other paths through the state machine:
//
// 1. The future is dropped (i.e. destroyed beforing being connected, or
//    destroyed after connect but before start) before the spawned operation
//    completes.  In this scenario, we request stop on the spawned operation and
//    then move to the complete state; when the operation ultimately completes,
//    it observes the complete state and cleans up.
//
// 2. The future is dropped after the spawned operation completes.  In this
//    scenario, we'll observe that the operation is in the value, error, or done
//    state and clean up as a side effect of dropping the future.
//
// 3. The future is connected and started, but then receives a stop request
//    before the operation completes.  In this scenario,  we need to signal the
//    operation and then complete the future with set_done().  To do this, we
//    request stop on the spawned operation and then move the operation to the
//    abandoned state; as the future completes with done in this scenario, we
//    need to negotiate who will delete the spawned operation: the now-completed
//    future or the spawned operation?  We resolve this negotiation by having
//    both sides race to see who can move the state from abandoned to complete;
//    whoever *fails* to update the state is deemed responsible for cleaning up.
//
// 4. The future is connected and started, but then receives a stop request
//    after the operation completes.  In this final scenario, the stop request
//    is effectively ignored.  The operation will move the state from init to
//    one of value, error, or done, and then the future will complete with the
//    appropriate result as if the stop request was never received.
//
// All of the above-described state transitions happen with atomic updates to
// the operation's state_ field so any "happens-before" relationships are as
// observed by those atomic operations.
enum class _future_state : unsigned char {
  // the operation has been constructed, and perhaps started
  init,
  // the future received a stop request and managed to signal so before the
  // operation completed
  abandoned,
  // the operation completed with set_value(); the values_ member has been
  // constructed
  value,
  // the operation completed with set_error(); the error_ member has been
  // constructed
  error,
  // the operation completed with set_done()
  done,
  // one of three things happened: a) the future was dropped, b) the future was
  // cancelled and it completed with done before the spawned operation
  // completed, or c) the future was cancelled and the spawned operation
  // completed before the future could complete with done; regardless, whoever
  // (future or operation) observes the complete state is responsible for
  // deleting the operation
  complete,
};

template <typename Scope, typename... T>
struct _future final {
  struct type;
};

template <typename Alloc>
struct _spawn_future_op_alloc final {
  struct type;
};

// an allocator-holder; this type exists to let us lay out _spawn_future_op_impl
// with the allocator before the _spawn_future_op_base in memory
template <typename Alloc>
struct _spawn_future_op_alloc<Alloc>::type {
  // standardize on allocators of std::bytes to minimize template
  // instantiations
  static_assert(same_as<std::byte, typename Alloc::value_type>);

  UNIFEX_NO_UNIQUE_ADDRESS Alloc alloc_;
};

// the type-indepedent base class for the spawned operation's operation state
struct _spawn_future_op_base {
  explicit _spawn_future_op_base(
      void (*destructOp)(_spawn_future_op_base*) noexcept,
      void (*deleter)(_spawn_future_op_base*, _future_state) noexcept) noexcept
    : destructOp_(destructOp)
    , deleter_(deleter) {}

  _spawn_future_op_base(_spawn_future_op_base&&) = delete;

  ~_spawn_future_op_base() = default;

  // invoked when the waiting future receives a stop request; attempts to cancel
  // the spawned operation and ensure that the waiting future is awoken promptly
  void abandon() noexcept {
    // abandon() is only invoked from a stop callback that's registered when the
    // future is awaited; we know for sure that the future has been started and
    // will consume the spawned operation.  if abandonment fails, it means the
    // stop request has lost the race to a natural completion and the future
    // will produce whatever the operation produced.

    // the operation must either be in the init state or one of the natural
    // completion states; in the former case we want to mark it as abandoned and
    // in the latter we'll just allow the future to complete naturally
    auto expected = _future_state::init;
    if (state_.compare_exchange_strong(
            expected,
            _future_state::abandoned,
            // on success, there are two audiences to publish to: the waiting
            // future and the still-running operation; the future will
            // synchronize through evt_ and see everything we've written; the
            // still running operation will eventually complete and have to
            // negotiate completion with the completing future so relaxed is
            // fine at this point.
            //
            // on failure, we've lost a completion race with the spawned
            // operation, in which case the state is a completion signal and the
            // operation is waking up the future with evt_.set(), which causes a
            // synchronization between operation and future; the attempted
            // abandonment "never happened" and so there's nothing to
            // synchronize
            std::memory_order_relaxed)) {
      // we set the state to abandoned; the future will complete with set_done()

      stopSource_.request_stop();

      // publish the result
      evt_.set();
    } else {
      UNIFEX_ASSERT(
          expected == _future_state::value ||
          expected == _future_state::error || expected == _future_state::done);
    }
  }

  // invoked by the spawned operation when it completes; attempts to record the
  // operation's completion style (value, error, or done) and store the result
  // in case of value or error.
  template <typename Func>
  void complete(_future_state desired, Func func) noexcept {
    UNIFEX_ASSERT(
        desired == _future_state::value || desired == _future_state::error ||
        desired == _future_state::done);

    // the happy path is that we transition from init to the desired state
    auto expected = _future_state::init;
    if (state_.compare_exchange_strong(
            expected,
            desired,
            // on success, we'll invoke evt_.set() to publish the writes that
            // happen in func() and destruct_op(); if the future is ever
            // started, it will synchronize through evt_ to consume the result;
            // if the future is never started then drop() will observe that the
            // future was dropped after the spawned operation completed and
            // synchronize through evt_.ready() before deleting this operation
            // state
            std::memory_order_relaxed,
            // on failure we need to consume the future's writes in case
            // negotiate_deletion() decides the next step is to delete this
            // operation state
            std::memory_order_acquire)) {
      // we successfully set the state to a completion signal (value, error, or
      // done) so invoke func to store the operation's result
      func();

      // func() has stored whatever it needs to in either values_ or error_ so
      // we can safely destruct the nested operation state now.  we have to do
      // this *before* waking up the future because the future might wake up on
      // another thread and delete the whole spawned operation while we're busy
      // trying to destroy a sub-part.  we also need to do this first because
      // we're relying on the event's memory ordering semantics to publish our
      // effects to the future and, since we've succeeded in completing here,
      // the future is going to be the one to destroy the overall operation,
      // which it should do *after* the nested operation has been destructed.
      destruct_op();

      // now that the spawned operation is really done, wake up the future and
      // let it take things from here
      evt_.set();
    } else {
      // the future has disappeared before we completed so we need to coordinate
      // deletion of this operation state

      // If expected is abandoned then the future is currently responding to a
      // stop request; if expected is complete then either the future was
      // dropped before being started or it was cancelled and has already
      // completed.  There's no other valid state here.
      UNIFEX_ASSERT(
          expected == _future_state::abandoned ||
          expected == _future_state::complete);

      // Figure out which case we're in and delete the operation state; do this
      // in a helper function because it's not dependent on Func
      negotiate_deletion(expected);
    }
  }

  void negotiate_deletion(_future_state expected) noexcept {
    // the spawned operation has completed so we can unconditionally destruct it
    destruct_op();

    if (expected == _future_state::abandoned) {
      // the future abandoned the operation but hasn't dropped its ownership
      // stake, yet; we need to coordinate who's going to delete the operation
      // state
      if (state_.compare_exchange_strong(
              expected,
              _future_state::complete,
              // on success we need to publish our writes to the future's thread
              std::memory_order_release,
              // on failure we need to observe the future's writes
              std::memory_order_acquire)) {
        // we've handed ownership of the operation state to the future so just
        // bail out
        return;
      }
    }

    // the future has disappeared and we own clean-up; it could be that the
    // future was already gone when we tried to set state_ to a completion
    // signal, or it could be that we observed the abandoned state and then lost
    // the race to set state_ to complete
    UNIFEX_ASSERT(expected == _future_state::complete);

    // we own deletion
    deleter_(this, expected);
  }

  // invoked by the future if it's dropped before being started
  void drop() noexcept {
    auto state = state_.load(
        // either we'll see init, in which case we'll do more synchronizing, or
        // we'll see a completion signal, in which case we'll synchronize
        // through evt_
        std::memory_order_relaxed);

    switch (state) {
      case _future_state::init:
        // we're being dropped before the spawned operation has completed;
        // request stop to hurry it up
        stopSource_.request_stop();

        // we want to give the spawned operation responsibility to delete the
        // operation state but it might have finished since we read the state as
        // init so try to assign the complete state with a CAS
        if (state_.compare_exchange_strong(
                state,
                _future_state::complete,
                // on success we need to publish our writes to the spawned
                // operation
                std::memory_order_release,
                // on failure we'll synchronize the operation's final write by
                // reading evt_
                std::memory_order_relaxed)) {
          // we've assigned clean-up responsibility to the not-yet-completed
          // operation; we're done

          return;
        }

        // we lost the race; this means the operation completed and we're
        // responsible for clean-up
        UNIFEX_ASSERT(
            state == _future_state::value || state == _future_state::error ||
            state == _future_state::done);

        [[fallthrough]];

      case _future_state::value:
      case _future_state::error:
      case _future_state::done:
        // we're being dropped after the operation finished; we need to clean up
        // the values_ or error_ that was written by the spawned operation
        // because the future isn't going to consume them naturally

        // reading evt_.ready() performs a load-acquire on the event so this is
        // how we consume the operation's last writes
        (void)evt_.ready();

        // having synchronized with evt_, we can now clean up
        deleter_(this, state);

        return;

      default:  // should never happen
        std::terminate();
    }
  }

  void destruct_op() noexcept { destructOp_(this); }

  template <typename Alloc>
  Alloc get_allocator() const noexcept {
    using byte_alloc_t =
        typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
    using op_alloc_t = typename _spawn_future_op_alloc<byte_alloc_t>::type;

    // see the class-level comment for _spawn_future_op_impl<>::type that
    // explains that type's memory layout
    //
    // we know that this points into the "middle" of the spawned operation,
    // right after the _spawn_future_op_alloc sub-object, so we can move
    // backwards from this by the size of a _spawn_future_op_alloc<Alloc> to get
    // to our allocator
    const std::byte* bytePtr = reinterpret_cast<const std::byte*>(this);

    bytePtr -= sizeof(op_alloc_t);

    const op_alloc_t* op = reinterpret_cast<const op_alloc_t*>(bytePtr);

    return op->alloc_;
  }

  // the event upon which the future waits; we signal it to wake up the future
  // once its result is known
  async_manual_reset_event evt_;
  // destroys the operation state for the spawned operation
  void (*destructOp_)(_spawn_future_op_base*) noexcept;
  // deletes this object
  void (*deleter_)(_spawn_future_op_base*, _future_state) noexcept;
  // the stop source from which the spawned operation gets stop tokens
  inplace_stop_source stopSource_;
  // the operation's current state; see the comments on the _future_state enum
  // for an explanation of the state machine
  std::atomic<_future_state> state_{_future_state::init};
};

template <typename... T>
struct _spawn_future_op final {
  struct type;
};

// the "middle" class in the spawned operation's operation state type hierarchy;
// it depends on the types of values the spawned operation can produce, but not
// on the Sender itself
//
// this type is primarily responsible for providing storage for the operation's
// results
template <typename... T>
struct _spawn_future_op<T...>::type : _spawn_future_op_base {
  explicit type(
      void (*destroyOp)(_spawn_future_op_base*) noexcept,
      void (*deleter)(_spawn_future_op_base*, _future_state) noexcept) noexcept
    : _spawn_future_op_base(destroyOp, deleter) {}

  type(type&&) = delete;

  ~type() {}

  // returns a Sender that produces the values produced by the spawned
  // operation
  auto get_value_sender() noexcept(
      noexcept(apply(just, std::move(this->values_).get()))) {
    return apply(just, std::move(values_).get());
  }

  // returns a Sender that produces the error produced by the spawned operation
  auto get_error_sender() noexcept(
      noexcept(just_error(std::move(this->error_).get()))) {
    return just_error(std::move(error_).get());
  }

  // storage for the result of the spawned operation
  union {
    manual_lifetime<std::tuple<T...>> values_;
    manual_lifetime<std::exception_ptr> error_;
  };

  template <typename Scope>
  using future_t = typename _future<Scope, T...>::type;
};

// a helper to map a Sender to the _spawn_future_op<...>::type that will be its
// operation state; the general case is just single_overload<>
template <typename... Overloads>
struct choose_spawn_future_op final : single_overload<Overloads...> {};

// in the special case of a Sender that does not complete with set_value(),
// choose _spawn_future_op<>::type; in other words, map a Sender that doesn't
// succeed to a Sender of no values
template <>
struct choose_spawn_future_op<> final {
  using type = _spawn_future_op<>;
};

// maps a Sender to the appropriate instantiation of _spawn_future_op<...>::type
// with some help from choose_spawn_future_op, above
template <typename Sender>
using spawn_future_op_for = typename sender_value_types_t<
    Sender,
    choose_spawn_future_op,
    _spawn_future_op>::type::type;

// the type-independent base class for the spawned operation's receiver
struct _spawn_future_receiver_base {
  _spawn_future_op_base* op_;

  void set_done() noexcept {
    op_->complete(_future_state::done, []() noexcept {
      // we've set the state to done but there's nothing else to store so this
      // is a no-op
    });
  }

  friend inplace_stop_token tag_invoke(
      tag_t<get_stop_token>, const _spawn_future_receiver_base& r) noexcept {
    return r.op_->stopSource_.get_token();
  }
};

template <typename Alloc>
struct _spawn_future_receiver_alloc final {
  struct type;
};

// this type injects into the fully-composed receiver's scope an implementation
// of get_allocator that depends only on the allocator's type
template <typename Alloc>
struct _spawn_future_receiver_alloc<Alloc>::type {
  friend Alloc tag_invoke(tag_t<get_allocator>, const type& r) noexcept {
    // _spawn_future_receiver_impl's constructor contains static_asserts that
    // guarantee the validity of this reinterpret_cast
    auto baseReceiver =
        reinterpret_cast<const _spawn_future_receiver_base*>(&r);

    return baseReceiver->op_->template get_allocator<Alloc>();
  }
};

template <typename... T>
struct _spawn_future_receiver final {
  struct type;
};

// a "middle" class in the spawned operation's receiver's type hierarchy; this
// is the class that knows the types of values produced by the spawned operation
template <typename... T>
struct _spawn_future_receiver<T...>::type : _spawn_future_receiver_base {
  using op_t = typename _spawn_future_op<T...>::type;

  explicit type(op_t* op) noexcept : _spawn_future_receiver_base{op} {}

  template <typename... Values>
  void set_value(Values&&... values) noexcept {
    op()->complete(_future_state::value, [&, op = op()]() noexcept {
      // we set the state to value
      UNIFEX_TRY {
        activate_union_member(op->values_, static_cast<Values&&>(values)...);
      }
      UNIFEX_CATCH(...) {
        // we failed to store the value so update the state and store the
        // current exception
        op->state_.store(
            _future_state::error,
            // since we made it here, the curent value of state_ is value, which
            // was stored with release semantics inside complete()
            //
            // updating from value to error with relaxed semantics is fine
            // because the write to op->error_ will be published when complete()
            // sets evt_ after we return
            std::memory_order_relaxed);
        activate_union_member(op->error_, std::current_exception());
      }
    });
  }

  void set_error(std::exception_ptr&& e) noexcept {
    op()->complete(_future_state::error, [&, op = op()]() noexcept {
      // we set the state to error
      activate_union_member(op->error_, std::move(e));
    });
  }

  op_t* op() const noexcept { return static_cast<op_t*>(this->op_); }
};

template <typename Alloc, typename... T>
struct _spawn_future_receiver_impl final {
  struct type;
};

// the fully-composed receiver for the spawned operation; this type combines
// knowledge of both the operation's value types and its allocator
template <typename Alloc, typename... T>
struct _spawn_future_receiver_impl<Alloc, T...>::type final
  : _spawn_future_receiver_alloc<Alloc>::type
  , _spawn_future_receiver<T...>::type {
  static_assert(same_as<std::byte, typename Alloc::value_type>);

  using op_t = typename _spawn_future_op<T...>::type;

  explicit type(op_t* op) noexcept : _spawn_future_receiver<T...>::type{op} {
    // offsetof is only defined behaviour on standard-layout types
    static_assert(std::is_standard_layout_v<type>);

    // we rely on op_ being at offset zero in the implementation of
    // get_allocator; if this assertion fails, we can't safely reinterpret_cast
    // a _spawn_future_receiver_alloc<Alloc>::type* to a
    // _spawn_future_receiver_base*
    static_assert(offsetof(type, op_) == 0);
  }
};

// a metafunction that has curried a value type pack so that, provided an
// allocator type, you can choose a fully-composed _spawn_future_receiver_impl
template <typename... T>
struct alloc_binder final {
  template <typename Alloc>
  using bind = typename _spawn_future_receiver_impl<Alloc, T...>::type;
};

template <typename... Overloads>
struct choose_spawn_future_receiver final : single_overload<Overloads...> {};

template <>
struct choose_spawn_future_receiver<> final {
  // map "does not invoke set_value" to "invokes set_value with no arguments"
  using type = alloc_binder<>;
};

// maps a Sender and Allocator to the fully-composed _spawn_future_receiver_impl
// that will consume the sender's result
template <typename Sender, typename Alloc>
using spawn_future_receiver_for = typename sender_value_types_t<
    Sender,
    choose_spawn_future_receiver,
    alloc_binder>::type::template bind<Alloc>;

template <typename Sender, typename Scope, typename Alloc>
struct _spawn_future_op_impl final {
  struct type;
};

// the complete spawned operation
//
// The memory layout for this object is as follows:
//
// _spawn_future_op_impl<Sender, Scope, Alloc>
//   _spawn_future_op_alloc<Alloc>
//     alloc_
//   _spawn_future_op<T...> // T... is computed from Sender
//     _spawn_future_op_base
//       // common members
//     values_/error_
//   op_
//
// The Receiver inside op_ knows the address of the _spawn_future_op<T...> and
// it knows the Alloc type and can thus compute the size of
// _spawn_future_op_alloc<Alloc>.
template <typename Sender, typename Scope, typename Alloc>
struct _spawn_future_op_impl<Sender, Scope, Alloc>::type final
  : _spawn_future_op_alloc<Alloc>::type
  , spawn_future_op_for<Sender> {
  // standardize on allocators of std::bytes to minimize template
  // instantiations
  static_assert(same_as<std::byte, typename Alloc::value_type>);

  using nest_sender_t =
      decltype(nest(UNIFEX_DECLVAL(Sender), UNIFEX_DECLVAL(Scope&)));

  using receiver_t = spawn_future_receiver_for<Sender, Alloc>;

  using op_t = connect_result_t<nest_sender_t, receiver_t>;

  explicit type(const Alloc& alloc) noexcept
    : _spawn_future_op_alloc<Alloc>::type{alloc}
    , spawn_future_op_for<Sender>{&destroy_operation, &deleter} {}

  type(type&&) = delete;

  template <typename Sender2>
  void init_operation(Sender2&& sender, Scope& scope, type* self) noexcept(
      noexcept(nest(static_cast<Sender2&&>(sender), scope)) &&
      is_nothrow_connectable_v<nest_sender_t, receiver_t>) {
    op_.construct_with([&]() {
      return connect(
          nest(static_cast<Sender2&&>(sender), scope), receiver_t{self});
    });
  }

  static void destroy_operation(_spawn_future_op_base* self) noexcept {
    static_cast<type*>(self)->op_.destruct();
  }

  static void
  deleter(_spawn_future_op_base* base, _future_state state) noexcept {
    // get an allocator for *this* type
    using alloc_t =
        typename std::allocator_traits<Alloc>::template rebind_alloc<type>;

    auto self = static_cast<type*>(base);
    alloc_t alloc = self->alloc_;

    // state is a function argument to avoid reloading the current state from
    // the state_ member when we already know its current value
    UNIFEX_ASSERT(state == self->state_.load(std::memory_order_relaxed));

    if (state == _future_state::value) {
      deactivate_union_member(self->values_);
    } else if (state == _future_state::error) {
      deactivate_union_member(self->error_);
    }

    std::allocator_traits<alloc_t>::destroy(alloc, self);
    std::allocator_traits<alloc_t>::deallocate(alloc, self, 1);
  }

  friend void tag_invoke(tag_t<start>, type& op) noexcept {
    start(op.op_.get());
  }

  manual_lifetime<op_t> op_;

  using future_t =
      typename spawn_future_op_for<Sender>::template future_t<Scope>;
};

// a callable, used in _future_sender_from_stop_token, that constructs a stop
// callback to respond to stop requests on future<>s by abandoning the related
// operation.
struct _future_stop_callback_factory final {
  _spawn_future_op_base* op_;
  inplace_stop_token stopToken_;

  auto operator()() noexcept {
    auto stopCallback = [op = op_]() noexcept {
      op->abandon();
    };

    using stop_callback_t =
        inplace_stop_token::callback_type<decltype(stopCallback)>;

    return stop_callback_t{stopToken_, stopCallback};
  }
};

// this type is used immediately below as a std::unique_ptr<>-compatible
// deleter; it "deletes" the spawned operation by invoking its drop() method
// to signal that the future has been dropped without being started
struct _op_dropper final {
  void operator()(_spawn_future_op_base* op) const noexcept {
    if (op) {
      op->drop();
    }
  }
};

template <typename... T>
struct _future_sender_from_stop_token final {
  struct type;
};

// A callable that returns a Sender given an inplace_stop_token.  An instance of
// this type is given to let_value_with_stop_token while constructing a
// future<>.
template <typename... T>
struct _future_sender_from_stop_token<T...>::type final {
  using spawn_future_op_t = typename _spawn_future_op<T...>::type;

  // an owning handle type that drops a spawned operation upon destruction
  using op_handle = std::unique_ptr<spawn_future_op_t, _op_dropper>;

  op_handle op_;

  explicit type(spawn_future_op_t* op) noexcept : op_(op) {}

  auto operator()(inplace_stop_token stopToken) noexcept {
    return let_value_with(
        _future_stop_callback_factory{op_.get(), stopToken},
        [this](auto&) noexcept {
          return let_value(
              op_->evt_.async_wait(),
              [this]() noexcept(
                  noexcept(op_->get_value_sender(), op_->get_error_sender())) {
                auto rawOp = op_.release();

                using value_t = decltype(op_->get_value_sender());
                using error_t = decltype(op_->get_error_sender());
                using done_t = decltype(just_done());

                using return_t = variant_sender<value_t, error_t, done_t>;

                auto state = rawOp->state_.load(std::memory_order_relaxed);

                // we capture state by reference because it may be updated by
                // the compare_exchange_strong below
                scope_guard cleanup = [rawOp, &state]() noexcept {
                  rawOp->deleter_(rawOp, state);
                };

                if (state == _future_state::abandoned) {
                  if (rawOp->state_.compare_exchange_strong(
                          state,
                          _future_state::complete,
                          // on success, publish our writes to the still-running
                          // spawned operation
                          std::memory_order_release,
                          // on failure, consume the now-finished operation's
                          // writes
                          std::memory_order_acquire)) {
                    // we gave clean-up responsibility to the spawned operation
                    cleanup.release();
                  } else {
                    // we run this if the CAS failed, which should mean the
                    // spawned operation beat us to setting state to complete
                    UNIFEX_ASSERT(state == _future_state::complete);
                  }
                }

                switch (state) {
                  case _future_state::value:
                    return return_t{rawOp->get_value_sender()};

                  case _future_state::error:
                    return return_t{rawOp->get_error_sender()};

                  case _future_state::done:
                  case _future_state::abandoned:
                  case _future_state::complete: return return_t{just_done()};

                  default:
                    // doesn't happen
                    std::terminate();
                }
              });
        });
  }
};

// A future<Scope, T...> is a Sender of T... that, when awaited, completes with
// the result of an operation that was earlier spawned in an async_scope of type
// Scope.
//
// A future<> is nested within its associated scope, which means that a) it may
// have *failed* to nest, in which case there is no associated spawned operation
// and the future<> can only complete with set_done(), and b) if it was
// successfully nested then the future<> holds a reference on its associated
// scope until it is either discarded or completed.
template <typename Scope, typename... T>
struct [[nodiscard]] _future<Scope, T...>::type final {
private:
  using spawn_future_op_t = typename _spawn_future_op<T...>::type;

  // constructs the Sender
  static auto make_sender(spawn_future_op_t* op) noexcept {
    using callable = typename _future_sender_from_stop_token<T...>::type;

    return let_value_with_stop_token(callable{op});
  }

  using sender_t = decltype(nest(make_sender(nullptr), UNIFEX_DECLVAL(Scope&)));

  sender_t sender_;

  // this is just good hygiene but let's assert it because we depend on it for
  // *our* move operators to be nothrow
  static_assert(std::is_nothrow_move_constructible_v<sender_t>);
  static_assert(std::is_nothrow_move_assignable_v<sender_t>);

public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<T...>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  explicit type(
      Scope& scope,
      typename _spawn_future_op<T...>::type*
          op) noexcept(noexcept(nest(make_sender(op), scope)))
    : sender_(nest(make_sender(op), scope)) {}

  type(type&&) noexcept = default;

  ~type() = default;

  type& operator=(type&&) noexcept = default;

  template <typename Receiver>
  friend auto
  tag_invoke(tag_t<connect>, type&& self, Receiver&& receiver) noexcept(
      is_nothrow_connectable_v<sender_t, Receiver>) {
    return connect(std::move(self).sender_, static_cast<Receiver&&>(receiver));
  }

  friend auto tag_invoke(tag_t<blocking>, const type&) noexcept {
    // we're never when nest succeeds and always_inline when it fails
    return blocking_kind::maybe;
  }
};

template <typename Sender, typename Scope, typename Alloc>
using future_for = typename _spawn_future_op_impl<
    Sender,
    Scope,
    typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>>::
    type::future_t;

struct _spawn_future_fn {
private:
  struct deref;

public:
  template(
      typename Sender,
      typename Scope,
      typename Alloc = std::allocator<std::byte>)  //
      (requires typed_sender<remove_cvref_t<Sender>> AND
           std::is_invocable_v<tag_t<nest>, Sender, Scope&> AND
               is_allocator_v<Alloc>)  //
      auto
      operator()(Sender&& sender, Scope& scope, const Alloc& alloc = {}) const {
    // We need to do several things here and the ordering is nuanced:
    //  - We should provide the Strong Exception Guarantee: if an exception is
    //    thrown then the state of the programme should be rolled back to the
    //    state just before we were called; this means no memory leaks, no
    //    running operations, and no leaked references to the given scope. There
    //    are comments interwoven with the implementation that explain how we
    //    meet this requirement.
    //  - We'll be nesting two Senders in the given scope (the given Sender and
    //    the future that we return).  We must tolerate the scope being
    //    closed a) before we start, b) between nesting the two Senders, and
    //    c) after we've nested both Senders.  Handling c) is trivial; a) is
    //    not hard; b) is tricky.
    //
    // Considering the ordering of nesting Senders, there are two options:
    //  1. If we nest the spawned operation before nesting the future then we
    //     risk the former succeeding and the latter failing, which would
    //     leave us with a running operation whose result can't be observed
    //     (because a failed nest() is analogous to just_done()--it will
    //     complete synchronously with set_done() once observed).
    //  2. If we nest the future before nesting the operation then we avoid the
    //     above problem; if the former succeeds and the latter fails then
    //     we have a successfully-nested future that, when awaited, will
    //     observe the failed operation as having synchronously completed
    //     with set_done().
    //
    // We choose option 2 to ensure we don't waste time running an unobservable
    // operation.  Obviously, if both nest operations succeed, the future can be
    // used to observe the result of the spawned operation. If both nest
    // operations fail then we'll have allocated an operation state in which the
    // spawned operation completes with set_done() and we'll have constructed a
    // future that will complete with set_done() without observing the allocated
    // operation state.
    //
    // The obvious downside to this algorithm is that we sometimes make
    // allocations that could, perhaps, be elided.  In the case that nesting
    // the spawned operation fails, we allocate space for an operation that,
    // in principle, we know will complete with set_done() so it seems like
    // we could skip the allocation and just mark the future as cancelled;
    // given that the future is nested successfully, though, the current
    // implementation unifies successful and unsuccessful nesting of the
    // spawned operation: a successfully nested future always consults the
    // spawned operation state for its completion.  In the case that both
    // nests fail, the heap allocation seems particularly useless because
    // the future won't even consult it for its result.  It's not obvious to
    // me how to detect that a nest operation has failed so I don't know how
    // to elide unnecessary allocations.  Perhaps we could elide the
    // allocation if there were a "reserve" operation on an async_scope.

    // rebind Alloc to be a std::byte allocator; this ensures that
    // _spawn_future_op_impl is always instantiated with a std::byte allocator,
    // cutting down on a potential explosion of template instantiations.
    using byte_alloc_t =
        typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;

    using op_t = typename _spawn_future_op_impl<
        remove_cvref_t<Sender>,
        Scope,
        byte_alloc_t>::type;

    // rebind again because we need to allocate op_ts, not std::bytes; we need
    // both an allocator and its std::allocator_traits
    using alloc_traits_t =
        typename std::allocator_traits<Alloc>::template rebind_traits<op_t>;
    using op_alloc_t = typename alloc_traits_t::allocator_type;

    // now actually construct the necessary allocator; the spec says this has to
    // be a noexcept expression for Alloc to conform to the Allocator concept
    // but, since this is the first thing we're doing, if it throws in practice
    // we don't care--we can just let the exception escape
    op_alloc_t opAlloc{std::move(alloc)};

    // allocate space for the spawned operation; we expect this to throw if the
    // allocation fails but, again, we don't care--letting the exception escape
    // is the right thing to do
    auto* op = alloc_traits_t::allocate(opAlloc, 1);

    // now construct the spawned operation in the just-allocated space; we can't
    // assert that this is noexcept because
    // std::allocator_traits<T>::construct() is declared to throw rather than
    // being conditionally-noexcept, but we know that the underlying constructor
    // is noexcept so we'll assume this doesn't throw in practice
    alloc_traits_t::construct(opAlloc, op, opAlloc);

    // the next two steps might throw and we have to destroy op if one of them
    // does throw so set up a scope_guard that will do that for us;
    // conveniently, the deleter() static method does exactly the right thing
    scope_guard cleanUp = [op]() noexcept {
      // Constructing the future is *almost* no-throw--only the call to nest()
      // might throw--so the future will invoke drop() on the operation (moving
      // it from init to complete) as part of its destructor before this code
      // runs.
      op_t::deleter(op, _future_state::complete);
    };

    using future_t = future_for<remove_cvref_t<Sender>, Scope, byte_alloc_t>;

    // construct the future that we'll hand back to the caller; this is fairly
    // likely to be noexcept since it's not much more than a Sender construction
    // but it depends on the implementation of nest() for scope so it might
    // throw
    future_t future{scope, op};

    // now finally construct the spawned operation, which might throw
    op->init_operation(static_cast<Sender&&>(sender), scope, op);

    // now that everything is wired together there are no more exception
    // concerns
    cleanUp.release();

    // start the spawned operation
    start(*op);

    // ideally, the compiler performs NRVO and constructs this future in-place
    // at the call site but future<>'s move constructor is noexcept so even if
    // there's an actual move here, there's still no chance of an exception
    return future;
  }

  template <typename Scope, typename Alloc = std::allocator<std::byte>>
  constexpr auto operator()(Scope& scope, const Alloc& alloc = {}) const
      noexcept(
          is_nothrow_callable_v<tag_t<bind_back>, deref, Scope*, const Alloc&>)
          -> std::enable_if_t<
              is_allocator_v<Alloc>,
              bind_back_result_t<deref, Scope*, const Alloc&>>;
};

struct _spawn_future_fn::deref final {
  template <typename Sender, typename Scope, typename Alloc>
  auto operator()(Sender&& sender, Scope* scope, const Alloc& alloc) const
      -> decltype(
          _spawn_future_fn{}(static_cast<Sender&&>(sender), *scope, alloc)) {
    return _spawn_future_fn{}(static_cast<Sender&&>(sender), *scope, alloc);
  }
};

template <typename Scope, typename Alloc>
inline constexpr auto
_spawn_future_fn::operator()(Scope& scope, const Alloc& alloc) const noexcept(
    is_nothrow_callable_v<tag_t<bind_back>, deref, Scope*, const Alloc&>)
    -> std::enable_if_t<
        is_allocator_v<Alloc>,
        bind_back_result_t<deref, Scope*, const Alloc&>> {
  return bind_back(deref{}, &scope, alloc);
}

}  // namespace _spawn_future

namespace v2 {

template <typename Scope, typename... T>
using future = typename _spawn_future::_future<Scope, T...>::type;

}  // namespace v2

inline constexpr _spawn_future::_spawn_future_fn spawn_future{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
