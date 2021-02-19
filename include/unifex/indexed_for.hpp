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
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/functional.hpp>
#include <unifex/utility.hpp>

#include <functional>
#include <type_traits>

namespace execution {
class sequenced_policy;
class parallel_policy;
}

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _ifor {
template <typename Policy, typename Range, typename Func, typename Receiver>
struct _receiver {
  struct type;
};
template <typename Policy, typename Range, typename Func, typename Receiver>
using receiver_t =
    typename _receiver<Policy, Range, Func, remove_cvref_t<Receiver>>::type;
template <typename Policy, typename Range, typename Func, typename Receiver>
struct _receiver<Policy, Range, Func, Receiver>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS Policy policy_;
  UNIFEX_NO_UNIQUE_ADDRESS Range range_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  // sequenced_policy version supports forward range
  template <typename... Values>
  static void apply_func_with_policy(const execution::sequenced_policy&, Range&& range, Func&& func, Values&... values)
      noexcept(is_nothrow_invocable_v<Func, typename std::iterator_traits<typename Range::iterator>::reference, Values...>) {
    for(auto idx : range) {
      unifex::invoke(func, idx, values...);
    }
  }

  // parallel_policy version requires random access range
  template <typename... Values>
  static void apply_func_with_policy(const execution::parallel_policy&, Range&& range, Func&& func, Values&... values)
      noexcept(is_nothrow_invocable_v<Func, typename std::iterator_traits<typename Range::iterator>::reference, Values...>) {
    auto first = range.begin();
    using size_type = decltype(range.size());
    for (size_type idx = 0; idx < range.size(); ++idx) {
      unifex::invoke(func, first[idx], values...);
    }
  }

  template <typename... Values>
  void set_value(Values&&... values) && noexcept {
    if constexpr (is_nothrow_invocable_v<Func&, typename std::iterator_traits<typename Range::iterator>::reference, Values...>) {
      apply_func_with_policy(policy_, (Range&&) range_, (Func &&) func_, values...);
      unifex::set_value((Receiver &&) receiver_, (Values &&) values...);
    } else {
      UNIFEX_TRY {
        apply_func_with_policy(policy_, (Range&&) range_, (Func &&) func_, values...);
        unifex::set_value((Receiver &&) receiver_, (Values &&) values...);
      } UNIFEX_CATCH (...) {
        unifex::set_error((Receiver &&) receiver_, std::current_exception());
      }
    }
  }

  template <typename Error>
  void set_error(Error&& error) && noexcept {
    unifex::set_error((Receiver &&) receiver_, (Error &&) error);
  }

  void set_done() && noexcept {
    unifex::set_done((Receiver &&) receiver_);
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO>)
  friend auto tag_invoke(CPO cpo, const type& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(unifex::as_const(r.receiver_));
  }

  template <typename Visit>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      Visit&& visit) {
    unifex::invoke(visit, r.receiver_);
  }
};

template <typename Predecessor, typename Policy, typename Range, typename Func>
struct _sender {
  struct type;
};
template <typename Predecessor, typename Policy, typename Range, typename Func>
using sender = typename _sender<
    remove_cvref_t<Predecessor>,
    std::decay_t<Policy>,
    std::decay_t<Range>,
    std::decay_t<Func>>::type;

template <typename Predecessor, typename Policy, typename Range, typename Func>
struct _sender<Predecessor, Policy, Range, Func>::type {
  using sender = type;
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Policy policy_;
  UNIFEX_NO_UNIQUE_ADDRESS Range range_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = sender_value_types_t<Predecessor, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types =
      typename concat_type_lists_unique_t<
          sender_error_types_t<Predecessor, type_list>,
          type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  friend constexpr auto tag_invoke(
      tag_t<blocking>,
      const sender& sender) {
    return blocking(sender.pred_);
  }

  template <typename Receiver>
  auto connect(Receiver&& receiver) && {
    return unifex::connect(
        std::forward<Predecessor>(pred_),
        _ifor::receiver_t<Policy, Range, Func, Receiver>{
            (Func &&) func_,
            (Policy &&) policy_,
            (Range &&) range_,
            (Receiver &&) receiver});
  }
};
} // namespace _ifor

namespace _ifor_cpo {
  struct _fn {
    template <typename Sender, typename Policy, typename Range, typename Func>
    auto operator()(Sender&& predecessor, Policy&& policy, Range&& range, Func&& func) const
        -> _ifor::sender<Sender, Policy, Range, Func> {
      return _ifor::sender<Sender, Policy, Range, Func>{
          (Sender &&) predecessor, (Policy &&) policy, (Range &&) range, (Func &&) func};
    }
    template <typename Policy, typename Range, typename Func>
    constexpr auto operator()(Policy&& policy, Range&& range, Func&& f) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, Policy, Range, Func>)
        -> bind_back_result_t<_fn, Policy, Range, Func> {
      return bind_back(*this, (Policy&&)policy, (Range&&)range, (Func&&)f);
    }
  } indexed_for{};
} // namespace _ifor_cpo
using _ifor_cpo::indexed_for;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
