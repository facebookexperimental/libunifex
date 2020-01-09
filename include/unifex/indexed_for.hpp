/*
 * Copyright 2019-present Facebook, Inc.
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
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>

#include <functional>
#include <type_traits>

namespace execution {
class sequenced_policy;
class parallel_policy;
}

namespace unifex {

template <typename Predecessor, typename Policy, typename Range, typename Func>
struct indexed_for_sender {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Policy policy_;
  UNIFEX_NO_UNIQUE_ADDRESS Range range_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

  template <template <typename...> class Tuple>
  struct indexed_for_result {
   public:
    template <typename... Args>
    using apply = Tuple<Args...>;
  };

  template <template <typename...> class Variant>
  struct calculate_errors {
   public:
    template <typename... Errors>
    using apply = deduplicate<Variant<Errors..., std::exception_ptr>>;
  };

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = deduplicate_t<typename Predecessor::template value_types<
      Variant,
      indexed_for_result<Tuple>::template apply>>;

  template <template <typename...> class Variant>
  using error_types = typename Predecessor::template error_types<
      calculate_errors<Variant>::template apply>;

  friend constexpr auto tag_invoke(
      tag_t<blocking>,
      const indexed_for_sender& sender) {
    return blocking(sender.pred_);
  }

  // sequenced_policy version supports forward range
  template<typename Range, typename... Values>
  static void apply_func_with_policy(const execution::sequenced_policy& policy, Range&& range, Func&& func, Values&... values)
      noexcept(std::is_nothrow_invocable_v<Func&, typename Range::iterator::value_type, Values...>) {
    for(auto idx : range) {
      std::invoke(func, idx, values...);
    }
  }

  // parallel_policy version requires random access range
  template<typename Range, typename... Values>
  static void apply_func_with_policy(const execution::parallel_policy& policy, Range&& range, Func&& func, Values&... values)
      noexcept(std::is_nothrow_invocable_v<Func&, typename Range::iterator::value_type, Values...>) {
    auto start = range.begin();
    for(auto idx = 0; idx < range.size(); ++idx) {
      std::invoke(func, start[idx], values...);
    }
  }

  template <typename Receiver>
  struct indexed_for_receiver {
    UNIFEX_NO_UNIQUE_ADDRESS Func func_;
    UNIFEX_NO_UNIQUE_ADDRESS Policy policy_;
    UNIFEX_NO_UNIQUE_ADDRESS Range range_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    template <typename... Values>
    void set_value(Values&&... values) && noexcept {
      using Range = std::invoke_result_t<indexed_for_detail::extract_range, RangeOrRangeSelector&&, Values&...>;
      decltype(auto) range = indexed_for_detail::extract_range{}(std::move(rangeOrSelector_), values...);
      if constexpr (
          std::is_nothrow_invocable_v<
            Func&, typename std::iterator_traits<typename Range::iterator>::reference, Values...>) {
        apply_func_with_policy(policy_, (Range&&) range, (Func &&) func_, values...);
        unifex::set_value((Receiver &&) receiver_, (Values &&) values...);
      } else {
        try {
          apply_func_with_policy(policy_, (Range&&) range_, (Func &&) func_, values...);
          unifex::set_value((Receiver &&) receiver_, (Values &&) values...);
        } catch (...) {
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

    template <
        typename CPO,
        std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
    friend auto tag_invoke(CPO cpo, const indexed_for_receiver& r) noexcept(
        std::is_nothrow_invocable_v<CPO, const Receiver&>)
        -> std::invoke_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.receiver_));
    }

    template <typename Visit>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const indexed_for_receiver& r,
        Visit&& visit) {
      std::invoke(visit, r.receiver_);
    }

  };

  template <typename Receiver>
  auto connect(Receiver&& receiver) && {
    return unifex::connect(
        std::forward<Predecessor>(pred_),
        indexed_for_receiver<std::remove_cvref_t<Receiver>>{
            std::forward<Func>(func_),
            std::forward<Policy>(policy_),
            std::forward<Range>(range_),
            std::forward<Receiver>(receiver)});
  }
};


// A version of the indexed_for sender that may be used when the Predecessor is
// known to be an indexed_for_sender. This does not chain using the receiver
// mechanism but directly applies pred_'s function to the data.
// This simulates how we would specialse on a custom type rather than the one
// provided by the standard library.
template <typename Predecessor, typename Policy, typename RangeOrRangeSelector, typename Func>
struct double_indexed_for_sender {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Policy policy_;
  UNIFEX_NO_UNIQUE_ADDRESS RangeOrRangeSelector rangeOrSelector_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

  template <template <typename...> class Tuple>
  struct double_indexed_for_result {
   public:
    template <typename... Args>
    using apply = Tuple<Args...>;
  };

  template <typename... Args>
  using is_overload_noexcept = std::bool_constant<noexcept(
      std::invoke(std::declval<Func>(), std::declval<Args>()...))>;

  template <template <typename...> class Variant>
  struct calculate_errors {
   public:
    template <typename... Errors>
    using apply = std::conditional_t<
        Predecessor::
            template value_types<std::conjunction, is_overload_noexcept>::value,
        Variant<Errors...>,
        deduplicate_t<Variant<Errors..., std::exception_ptr>>>;
  };

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = deduplicate_t<typename Predecessor::template value_types<
      Variant,
      double_indexed_for_result<Tuple>::template apply>>;

  template <template <typename...> class Variant>
  using error_types = typename Predecessor::template error_types<
      calculate_errors<Variant>::template apply>;

  friend constexpr auto tag_invoke(
      tag_t<blocking>,
      const double_indexed_for_sender& sender) {
    return blocking(sender.pred_);
  }

  // sequenced_policy version supports forward range
  template<typename Range, typename... Values>
  static void apply_func_with_policy(const execution::sequenced_policy& policy, Range&& range, Func&& func, Values&... values)
      noexcept(std::is_nothrow_invocable_v<Func&, typename Range::iterator::value_type, Values...>) {
    for(auto idx : range) {
      std::invoke(func, idx, values...);
    }
  }

  // parallel_policy version requires random access range
  template<typename Range, typename... Values>
  static void apply_func_with_policy(const execution::parallel_policy& policy, Range&& range, Func&& func, Values&... values)
      noexcept(std::is_nothrow_invocable_v<Func&, typename Range::iterator::value_type, Values...>) {
    auto start = range.begin();
    for(auto idx = 0; idx < range.size(); ++idx) {
      std::invoke(func, start[idx], values...);
    }
  }

  template <typename Receiver>
  struct double_indexed_for_receiver {
    UNIFEX_NO_UNIQUE_ADDRESS decltype(Predecessor::func_) predFunc_;
    UNIFEX_NO_UNIQUE_ADDRESS Func func_;
    UNIFEX_NO_UNIQUE_ADDRESS decltype(Predecessor::policy_) predPolicy_;
    UNIFEX_NO_UNIQUE_ADDRESS Policy policy_;
    UNIFEX_NO_UNIQUE_ADDRESS decltype(Predecessor::rangeOrSelector_) predRangeOrSelector_;
    UNIFEX_NO_UNIQUE_ADDRESS RangeOrRangeSelector rangeOrSelector_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    template <typename... Values>
    void set_value(Values&&... values) && noexcept {
      using PredRange = std::invoke_result_t<indexed_for_detail::extract_range, decltype(Predecessor::rangeOrSelector_)&&, Values&...>;
      decltype(auto) pred_range = indexed_for_detail::extract_range{}(std::move(predRangeOrSelector_), values...);
      using Range = std::invoke_result_t<indexed_for_detail::extract_range, RangeOrRangeSelector&&, Values&...>;
      decltype(auto) range = indexed_for_detail::extract_range{}(std::move(rangeOrSelector_), values...);

      if constexpr (std::is_nothrow_invocable_v<Func, typename Range::iterator::value_type, Values...> &&
                    std::is_nothrow_invocable_v<decltype(Predecessor::func_), typename Range::iterator::value_type, Values...>) {
        // Apply predecessors operation first, then the immediate sender's
        Predecessor::apply_func_with_policy(predPolicy_, (PredRange&&) pred_range, (decltype(Predecessor::func_) &&) predFunc_, values...);
        apply_func_with_policy(policy_, (Range&&) range, (Func &&) func_, values...);
        unifex::set_value((Receiver &&) receiver_, (Values &&) values...);
      } else {
        try {
          Predecessor::apply_func_with_policy(predPolicy_, (PredRange&&) pred_range, (decltype(Predecessor::func_) &&) predFunc_, values...);
          apply_func_with_policy(policy_, (Range&&) range, (Func &&) func_, values...);
          unifex::set_value((Receiver &&) receiver_, (Values &&) values...);
        } catch (...) {
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

    template <
        typename CPO,
        std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
    friend auto tag_invoke(CPO cpo, const double_indexed_for_receiver& r) noexcept(
        std::is_nothrow_invocable_v<CPO, const Receiver&>)
        -> std::invoke_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.receiver_));
    }

    template <typename Visit>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const double_indexed_for_receiver& r,
        Visit&& visit) {
      std::invoke(visit, r.receiver_);
    }

  };

  template <typename Receiver>
  auto connect(Receiver&& receiver) && {
    // Connect the receiver to the predecssor of pred_
    // we skip the connect/start pairing on the immediate
    // predecessor as an optimisation (or at least, to
    // emulate the optimisation we might see in
    // accelerator implementations)
    return unifex::connect(
        std::forward<Predecessor>(pred_).pred_,
        double_indexed_for_receiver<std::remove_cvref_t<Receiver>>{
            std::forward<Predecessor>(pred_).func_,
            std::forward<Func>(func_),
            std::forward<Predecessor>(pred_).policy_,
            std::forward<Policy>(policy_),
            std::forward<Predecessor>(pred_).rangeOrSelector_,
            std::forward<RangeOrRangeSelector>(rangeOrSelector_),
            std::forward<Receiver>(receiver)});
  }
};

struct indexed_for_cpo {
  // Default version of CPO that returns a sender tied directly together
  template <typename Sender, typename Policy, typename RangeOrRangeSelector, typename Func>
  friend auto
  tag_invoke(indexed_for_cpo, Sender&& predecessor, Policy&& policy, RangeOrRangeSelector&& rangeOrSelector, Func&& func) noexcept {
    return indexed_for_sender<std::remove_cvref_t<Sender>, std::decay_t<Policy>, std::decay_t<RangeOrRangeSelector>, std::decay_t<Func>>{
        (Sender &&) predecessor, (Policy&&) policy, (RangeOrRangeSelector&& ) rangeOrSelector, (Func &&) func};
  }

  template <typename Sender, typename Policy, typename RangeOrRangeSelector, typename Func>
  auto operator()( Sender&& predecessor, Policy&& policy, RangeOrRangeSelector&& rangeOrSelector, Func&& func) const
      noexcept(is_nothrow_tag_invocable_v<
               visit_continuations_cpo,
               Sender&&,
               Policy&&,
               RangeOrRangeSelector&&,
               Func&&>) {
    return tag_invoke(
      indexed_for_cpo{}, (Sender &&) predecessor, (Policy&&) policy, (RangeOrRangeSelector&& ) rangeOrSelector, (Func &&) func);
  }
};

// Customisation for when an indexed_for is chained after an indexed_for_sender
// This simulates how we might customise an arbitrary set of types for
// optimal interaction
template <typename InnerPredecessor, typename InnerPolicy, typename InnerRangeOrRangeSelector, typename InnerFunc, typename Policy, typename RangeOrRangeSelector, typename Func>
auto
tag_invoke(
    indexed_for_cpo,
    indexed_for_sender<InnerPredecessor, InnerPolicy, InnerRangeOrRangeSelector, InnerFunc>&& predecessor,
    Policy&& policy, RangeOrRangeSelector&& rangeOrSelector, Func&& func) noexcept {

  // Construct the doubled version of the sender that grabs content from the predecessor directly
  // and does not apply connect/start in between.
  return double_indexed_for_sender<
        std::remove_cvref_t<indexed_for_sender<InnerPredecessor, InnerPolicy, InnerRangeOrRangeSelector, InnerFunc>>,
        std::decay_t<Policy>, std::decay_t<RangeOrRangeSelector>, std::decay_t<Func>>{
      (indexed_for_sender<InnerPredecessor, InnerPolicy, InnerRangeOrRangeSelector, InnerFunc>&& ) predecessor,
      (Policy&&) policy,
      (RangeOrRangeSelector&& ) rangeOrSelector,
      (Func &&) func};
}

} // namespace indexed_for_detail

inline constexpr indexed_for_detail::indexed_for_cpo indexed_for;

} // namespace unifex
