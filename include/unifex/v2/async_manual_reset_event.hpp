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
#include <unifex/cancellable.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/detail/atomic_intrusive_list.hpp>

#include <unifex/detail/prologue.hpp>

#include <atomic>

namespace unifex::v2 {
// Lock-free async manual reset event.
//
// Uses the latch mode of atomic_intrusive_list to encode
// "signalled" directly in the waiter list's head pointer.
// set(), start(), and reset() all serialise through head_'s
// spinlock, eliminating race windows between signalling and
// draining.
//
// Scheduler-affine: completions reschedule onto the receiver's
// scheduler.  Cancellation: via try_remove on the waiter list.
class async_manual_reset_event {
  class wait_raw_sender;

public:
  async_manual_reset_event() noexcept = default;

  explicit async_manual_reset_event(bool startSignalled) noexcept {
    if (startSignalled) {
      set();
    }
  }

  void set() noexcept;

  [[nodiscard]] bool ready() const noexcept { return waiters_.is_latched(); }

  void reset() noexcept { waiters_.unlatch(); }

  [[nodiscard]] auto async_wait() noexcept;

private:
  struct waiter_base : atomic_intrusive_list_node {
    void (*resume_)(waiter_base*) noexcept;
  };

  atomic_intrusive_list<waiter_base, true> waiters_;

  class wait_raw_sender {
  public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;
    static constexpr blocking_kind blocking = blocking_kind::maybe;
    static constexpr bool is_always_scheduler_affine = true;

    wait_raw_sender(const wait_raw_sender&) = delete;
    wait_raw_sender(wait_raw_sender&&) = default;

  private:
    friend async_manual_reset_event;

    explicit wait_raw_sender(async_manual_reset_event& evt) noexcept
      : evt_(evt) {}

    template <typename Receiver>
    struct _op {
      class type : waiter_base {
        friend wait_raw_sender;
        friend struct reschedule_receiver;

        // Receiver for the rescheduling schedule() operation.
        // Overrides get_stop_token -> unstoppable_token so the
        // reschedule cannot be cancelled.  Forwards all other
        // queries to the original receiver.
        struct reschedule_receiver {
          void set_value() noexcept { op_.complete_value(); }

          template <typename Error>
          void set_error(Error&& error) noexcept {
            unifex::set_error(std::move(op_.receiver_), (Error&&)error);
          }

          void set_done() noexcept {
            unifex::set_done(std::move(op_.receiver_));
          }

          friend unstoppable_token tag_invoke(
              tag_t<get_stop_token>, const reschedule_receiver&) noexcept {
            return {};
          }

          template(typename CPO)                       //
              (requires is_receiver_query_cpo_v<CPO>)  //
              friend auto tag_invoke(
                  CPO cpo,
                  const reschedule_receiver&
                      r) noexcept(std::
                                      is_nothrow_invocable_v<
                                          CPO,
                                          const Receiver&>)
                  -> std::invoke_result_t<CPO, const Receiver&> {
            return std::move(cpo)(r.get_receiver_());
          }

          type& op_;

        private:
          // Route through a member to work around GCC 11's
          // inability to access enclosing-class privates from
          // a friend free function.
          const Receiver& get_receiver_() const noexcept {
            return op_.receiver_;
          }
        };

        using reschedule_op_t = decltype(unifex::connect(
            schedule(get_scheduler(UNIFEX_DECLVAL(const Receiver&))),
            UNIFEX_DECLVAL(reschedule_receiver)));

      public:
        explicit type(async_manual_reset_event& evt, Receiver&& r) noexcept
          : evt_(evt)
          , receiver_(std::forward<Receiver>(r)) {
          this->resume_ = [](waiter_base* self) noexcept {
            auto* op = static_cast<type*>(self);
            if (try_complete(op)) {
              op->reschedule();
            }
          };
        }

        type(type&&) = delete;

        void start() noexcept;
        void stop() noexcept;

      private:
        void reschedule() noexcept {
          reschedule_op_.construct_with([this]() noexcept {
            return unifex::connect(
                schedule(get_scheduler(receiver_)), reschedule_receiver{*this});
          });
          unifex::start(reschedule_op_.get());
        }

        void complete_value() noexcept {
#if !UNIFEX_NO_EXCEPTIONS
          try {
            unifex::set_value(std::move(receiver_));
          } catch (...) {
            unifex::set_error(std::move(receiver_), std::current_exception());
          }
#else
          unifex::set_value(std::move(receiver_));
#endif
        }

        async_manual_reset_event& evt_;
        Receiver receiver_;
        manual_lifetime<reschedule_op_t> reschedule_op_;
      };
    };

    template <typename Receiver>
    using operation = typename _op<Receiver>::type;

    template(typename Receiver)                                            //
        (requires receiver_of<Receiver> AND scheduler_provider<Receiver>)  //
        friend operation<Receiver> tag_invoke(
            tag_t<connect>, wait_raw_sender&& s, Receiver&& r) noexcept {
      return operation<Receiver>{s.evt_, std::forward<Receiver>(r)};
    }

    async_manual_reset_event& evt_;
  };
};

inline auto async_manual_reset_event::async_wait() noexcept {
  return cancellable<wait_raw_sender, false>{wait_raw_sender{*this}};
}

template <typename Receiver>
void async_manual_reset_event::wait_raw_sender::_op<
    Receiver>::type::start() noexcept {
  if (!evt_.waiters_.push_front_unless_latched(this)) {
    // Already signalled — complete via fast path.
    if (try_complete(this)) {
      reschedule();
    }
    return;
  }
}

template <typename Receiver>
void async_manual_reset_event::wait_raw_sender::_op<
    Receiver>::type::stop() noexcept {
  if (evt_.waiters_.try_remove(this)) {
    if (try_complete(this)) {
      unifex::set_done(std::move(receiver_));
    }
  }
}

}  // namespace unifex::v2

#include <unifex/detail/epilogue.hpp>
