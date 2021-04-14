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
#include <unifex/async_manual_reset_event.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/sequence.hpp>
#include <unifex/transform.hpp>
#include <unifex/type_traits.hpp>

#include <atomic>
#include <memory>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _async_scope {

struct async_scope;

struct _receiver_base {
  [[noreturn]] void set_error(std::exception_ptr) noexcept {
    std::terminate();
  }

  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const _receiver_base& r) noexcept {
    return r.stopToken_;
  }

  inplace_stop_token stopToken_;
  void* op_;
  async_scope* scope_;
};

template <typename Scheduler>
struct _has_scheduler {
  struct type;
};

template <typename Scheduler>
using has_scheduler = typename _has_scheduler<Scheduler>::type;

template <typename Scheduler>
struct _has_scheduler<Scheduler>::type {
  friend const Scheduler&
  tag_invoke(tag_t<get_scheduler>, const type& r) noexcept {
    return r.scheduler_;
  }

  UNIFEX_NO_UNIQUE_ADDRESS Scheduler scheduler_;
};

template <typename Sender, typename Scheduler>
struct _receiver {
  struct type;
};

template <typename Sender, typename Scheduler>
using receiver = typename _receiver<Sender, Scheduler>::type;

void record_done(async_scope*) noexcept;

template <typename Sender, typename Scheduler>
struct _receiver<Sender, Scheduler>::type final
    : _receiver_base, has_scheduler<Scheduler> {
  explicit type(
      const Scheduler& scheduler,
      inplace_stop_token stoken,
      void* op,
      async_scope* scope) noexcept
      : _receiver_base{stoken, op, scope},
        has_scheduler<Scheduler>{scheduler} {}

  // receivers uniquely own themselves; we don't need any special move-
  // construction behaviour, but we do need to ensure no copies are made
  type(type&&) noexcept = default;

  ~type() = default;

  // it's just simpler to skip this
  type& operator=(type&&) = delete;

  void set_value() noexcept {
    set_done();
  }

  void set_done() noexcept {
    // we're about to delete this, so save the scope for later
    auto scope = scope_;

    using op_t = manual_lifetime<connect_result_t<Sender, type>>;

    auto op = static_cast<op_t*>(op_);
    op->destruct();
    delete op;

    record_done(scope);
  }
};

struct async_scope {
  async_scope() noexcept = default;

  ~async_scope() {
    [[maybe_unused]] auto state = opState_.load(std::memory_order_relaxed);

    UNIFEX_ASSERT(is_stopping(state));
    UNIFEX_ASSERT(op_count(state) == 0);
  }

  template (typename Sender, typename Scheduler)
    (requires scheduler<Scheduler> AND
     sender_to<Sender, receiver<Sender, remove_cvref_t<Scheduler>>>)
  void spawn(Sender&& sender, Scheduler&& scheduler) {
    using receiver_t = receiver<Sender, remove_cvref_t<Scheduler>>;
    using op_t = connect_result_t<Sender, receiver_t>;

    // this could throw; if it does, there's nothing to clean up
    auto opToStart = std::make_unique<manual_lifetime<op_t>>();

    // this could throw; if it does, the only clean-up we need is to destroy
    // and deallocate the manual_lifetime, which is handled by opToStart's
    // destructor so we're good
    opToStart->construct_from([&] {
      return connect(
          (Sender&&)sender,
          receiver_t{
              (Scheduler&&)scheduler,
              stopSource_.get_token(),
              opToStart.get(),
              this});
    });

    // At this point, the rest of the function is noexcept, but opToStart's
    // destructor is no longer enough to properly clean up because it won't
    // invoke destruct().  We need to ensure that we either call destruct()
    // ourselves or complete the operation so *it* can call destruct().

    if (try_record_start()) {
      // start is noexcept so we can assume that the operation will complete
      // after this, which means we can rely on its self-ownership to ensure
      // that it is eventually deleted
      unifex::start(opToStart.release()->get());
    }
    else {
      // we've been stopped so clean up and bail out
      opToStart->destruct();
    }
  }

  [[nodiscard]] auto cleanup() noexcept {
    return sequence(
        transform(just(), [this]() noexcept {
          request_stop();
        }),
        transform(evt_.async_wait(), [this]() noexcept {
          // make sure to synchronize with all the fetch_subs being done while
          // operations complete
          (void)opState_.load(std::memory_order_acquire);
        }));
  }

 private:
  inplace_stop_source stopSource_;
  // (opState_ & 1) is 1 until we've been stopped
  // (opState_ >> 1) is the number of outstanding operations
  std::atomic<std::size_t> opState_{1};
  async_manual_reset_event evt_;

  static constexpr std::size_t stoppedBit{1};

  static bool is_stopping(std::size_t state) noexcept {
    return (state & stoppedBit) == 0;
  }

  static std::size_t op_count(std::size_t state) noexcept {
    return state >> 1;
  }

  [[nodiscard]] bool try_record_start() noexcept {
    auto opState = opState_.load(std::memory_order_relaxed);

    do {
      if (is_stopping(opState)) {
        return false;
      }

      UNIFEX_ASSERT(opState + 2 > opState);
    } while (!opState_.compare_exchange_weak(
        opState,
        opState + 2,
        std::memory_order_relaxed));

    return true;
  }

  friend void record_done(async_scope* scope) noexcept {
    auto oldState = scope->opState_.fetch_sub(2, std::memory_order_release);

    if (is_stopping(oldState) && op_count(oldState) == 1) {
      // the scope is stopping and we're the last op to finish
      scope->evt_.set();
    }
  }

  void request_stop() noexcept {
    // stop adding work
    auto oldState = opState_.fetch_and(~stoppedBit, std::memory_order_release);

    // request that existing work end soon
    stopSource_.request_stop();

    if (op_count(oldState) == 0) {
      // there are no outstanding operations to wait for
      evt_.set();
    }
  }
};

};

using _async_scope::async_scope;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
