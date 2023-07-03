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

#include <unifex/await_transform.hpp>
#include <unifex/connect_awaitable.hpp>
#include <unifex/finally.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/unstoppable.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _wsa {

template <typename Sender, typename Scheduler>
struct _wsa_sender_wrapper final {
  class type;
};

template <typename Sender, typename Scheduler>
static auto
_make_sender(Sender&& sender, Scheduler&& scheduler) noexcept(noexcept(finally(
    static_cast<Sender&&>(sender),
    unstoppable(schedule(static_cast<Scheduler&&>(scheduler)))))) {
  return finally(
      static_cast<Sender&&>(sender),
      unstoppable(schedule(static_cast<Scheduler&&>(scheduler))));
}

template <typename Sender, typename Scheduler>
using wsa_sender_wrapper =
    typename _wsa_sender_wrapper<Sender, Scheduler>::type;

template <typename Sender, typename Scheduler>
class _wsa_sender_wrapper<Sender, Scheduler>::type final {
  using sender_t =
      decltype(_make_sender(UNIFEX_DECLVAL(Sender), UNIFEX_DECLVAL(Scheduler)));

  sender_t sender_;

public:
  template <
      template <typename...>
      typename Variant,
      template <typename...>
      typename Tuple>
  using value_types = sender_value_types_t<sender_t, Variant, Tuple>;

  template <template <typename...> typename Variant>
  using error_types = sender_error_types_t<sender_t, Variant>;

  static constexpr bool sends_done = sender_traits<sender_t>::sends_done;

  static constexpr blocking_kind blocking = sender_traits<sender_t>::blocking;

  static constexpr bool is_always_scheduler_affine = true;

  template <typename Sender2, typename Scheduler2>
  type(Sender2&& sender, Scheduler2 scheduler) noexcept(noexcept(_make_sender(
      static_cast<Sender2&&>(sender), static_cast<Scheduler2&&>(scheduler))))
    : sender_(_make_sender(
          static_cast<Sender2&&>(sender),
          static_cast<Scheduler2&&>(scheduler))) {}

  template(typename Self, typename Receiver)          //
      (requires same_as<remove_cvref_t<Self>, type>)  //
      friend auto tag_invoke(tag_t<connect>, Self&& sender, Receiver&& receiver) noexcept(
          is_nothrow_connectable_v<member_t<Self, sender_t>, Receiver>) {
    return connect(
        static_cast<Self&&>(sender).sender_, static_cast<Receiver&&>(receiver));
  }
};

struct _fn final {
  template(typename Sender, typename Scheduler)          //
      (requires sender<Sender> AND scheduler<Scheduler>  //
           AND tag_invocable<_fn, Sender, Scheduler>)    //
      constexpr auto
      operator()(Sender&& s, Scheduler&& sched) const
      noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Scheduler>)
          -> tag_invoke_result_t<_fn, Sender, Scheduler> {
    // allow customization
    return tag_invoke(
        _fn{}, static_cast<Sender&&>(s), static_cast<Scheduler&&>(sched));
  }

  template(typename Sender, typename Scheduler)                               //
      (requires sender<Sender> AND scheduler<Scheduler> AND                   //
           sender_traits<remove_cvref_t<Sender>>::is_always_scheduler_affine  //
               AND(!tag_invocable<_fn, Sender, Scheduler>))                   //
      constexpr Sender&&
      operator()(Sender&& s, Scheduler&&) const noexcept {
    // the default implementation for statically-affine senders is the identity
    return static_cast<Sender&&>(s);
  }

  template(typename Sender, typename Scheduler)                              //
      (requires sender<Sender> AND scheduler<Scheduler> AND                  //
       (!sender_traits<remove_cvref_t<Sender>>::is_always_scheduler_affine)  //
       AND(!tag_invocable<_fn, Sender, Scheduler>))                          //
      constexpr auto
      operator()(Sender&& s, Scheduler&& sched) const
      noexcept(std::is_nothrow_constructible_v<
               wsa_sender_wrapper<
                   remove_cvref_t<Sender>,
                   remove_cvref_t<Scheduler>>,
               Sender,
               Scheduler>) {
    // the default implementation for non-affine senders is a via back to
    // the given scheduler
    using sender_t =
        wsa_sender_wrapper<remove_cvref_t<Sender>, remove_cvref_t<Scheduler>>;

    return sender_t{static_cast<Sender&&>(s), static_cast<Scheduler&&>(sched)};
  }

  template(typename Promise, typename Awaitable, typename Scheduler)  //
      (requires detail::_awaitable<Awaitable> AND(!sender<Awaitable>)
           AND scheduler<Scheduler>)  //
      constexpr decltype(auto)
      operator()(
          Promise& promise, Awaitable&& awaitable, Scheduler&& sched) const {
    using blocking_t = decltype(blocking(awaitable));

    // TODO: detect statically-affine awaitables
    // HACK: this is a hacky implementation of what used to be cblocking<>()
    if constexpr (
        !same_as<blocking_kind, blocking_t> &&
        (blocking_kind::always_inline == blocking_t{})) {
      return Awaitable{(Awaitable &&) awaitable};
    } else {
      // TODO: do this more efficiently; the current approach converts an
      //       awaitable to a sender so we can pass it to via, only to
      //       convert it back to an awaitable
      return unifex::await_transform(
          promise,
          operator()(
              as_sender(static_cast<Awaitable&&>(awaitable)),
              static_cast<Scheduler&&>(sched)));
    }
  }
};

}  // namespace _wsa

inline constexpr _wsa::_fn with_scheduler_affinity{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
