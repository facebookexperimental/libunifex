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
#include <unifex/execution_policy.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/type_list.hpp>
#include <unifex/std_concepts.hpp>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _find_if {
namespace detail {
  template <typename Result, typename = void>
  struct result_overload {
    using type = type_list<Result>;
  };
  template <typename Result>
  struct result_overload<Result, std::enable_if_t<std::is_void_v<Result>>> {
    using type = type_list<>;
  };
}

template <typename Receiver, typename Func, typename FuncPolicy>
struct _receiver {
  struct type;
};
template <typename Receiver, typename Func, typename FuncPolicy>
using receiver_t = typename _receiver<Receiver, Func, FuncPolicy>::type;

template <typename Receiver, typename Func, typename FuncPolicy>
struct _receiver<Receiver, Func, FuncPolicy>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  UNIFEX_NO_UNIQUE_ADDRESS FuncPolicy funcPolicy_;

  template<typename Iterator, typename... Values>
  auto find_if_helper(Iterator begin_it, Iterator end_it, const Values&... values) -> Iterator {
    // Scalar implementation
    for(auto it = begin_it; it != end_it; ++it) {
      if(std::invoke((Func &&) func_, *it, (Values &&) values...)) {
        return it;
      }
    }
    return end_it;
  }

  template <typename BeginIt, typename EndIt, typename... Values>
  void set_value(BeginIt begin_it, EndIt end_it, Values&&... values) && noexcept {
    if constexpr (noexcept(std::invoke(
                      (Func &&) func_, *begin_it, (Values &&) values...))) {

      auto result = find_if_helper(begin_it, end_it, values...);

      unifex::set_value((Receiver &&) receiver_, std::move(result), (Values &&) values...);
    } else {
      try {
        auto result = find_if_helper(begin_it, end_it, values...);

        unifex::set_value((Receiver &&) receiver_, std::move(result), (Values &&) values...);
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

  template(typename CPO, typename R)
      (requires is_receiver_query_cpo_v<CPO> AND same_as<R, type>)
  friend auto tag_invoke(CPO cpo, const R& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

  template <typename Visit>
  friend void tag_invoke(tag_t<visit_continuations>, const type& r, Visit&& visit) {
    std::invoke(visit, r.receiver_);
  }
};

template <typename Predecessor, typename Func, typename FuncPolicy>
struct _sender {
  struct type;
};
template <typename Predecessor, typename Func, typename FuncPolicy>
using sender = typename _sender<remove_cvref_t<Predecessor>, std::decay_t<Func>, FuncPolicy>::type;

template <typename Predecessor, typename Func, typename FuncPolicy>
struct _sender<Predecessor, Func, FuncPolicy>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS FuncPolicy funcPolicy_;

private:

  template <typename BeginIt, typename EndIt, typename... Args>
  using result = type_list<type_list<BeginIt, Args...>>;

public:

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = type_list_nested_apply_t<
    typename Predecessor::template value_types<concat_type_lists_unique_t, result>,
    Variant,
    Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
    typename Predecessor::template error_types<type_list>,
    type_list<std::exception_ptr>>::template apply<Variant>;

  template <typename Receiver>
  using receiver_t = receiver_t<Receiver, Func, FuncPolicy>;

  friend constexpr auto tag_invoke(tag_t<blocking>, const type& sender) {
    return blocking(sender.pred_);
  }

  template(typename Sender, typename Receiver)
    (requires same_as<remove_cvref_t<Sender>, type> AND receiver<Receiver>)
  friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r)
    noexcept(
      std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver> &&
      std::is_nothrow_constructible_v<Func, decltype((static_cast<Sender&&>(s).func_))> &&
      is_nothrow_connectable_v<decltype((static_cast<Sender&&>(s).pred_)), receiver_t<remove_cvref_t<Receiver>>>)
      -> connect_result_t<decltype((static_cast<Sender&&>(s).pred_)), receiver_t<remove_cvref_t<Receiver>>> {
    return unifex::connect(
      static_cast<Sender&&>(s).pred_,
      receiver_t<remove_cvref_t<Receiver>>{
        static_cast<Sender&&>(s).func_,
        static_cast<Receiver&&>(r),
        static_cast<Sender&&>(s).funcPolicy_});
  }
};
} // namespace _find_if

namespace _find_if_cpo {
  inline const struct _fn {
  private:
    template <typename Sender, typename Func, typename FuncPolicy>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, Sender, Func>,
        meta_tag_invoke_result<_fn>,
        meta_quote3<_find_if::sender>>::template apply<Sender, Func, FuncPolicy>;
  public:
    template(typename Sender, typename Func, typename FuncPolicy)
      (requires tag_invocable<_fn, Sender, Func, FuncPolicy>)
    auto operator()(Sender&& predecessor, Func&& func, FuncPolicy policy) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Func, FuncPolicy>)
        -> _result_t<Sender, Func, FuncPolicy> {
      return unifex::tag_invoke(_fn{}, (Sender&&)predecessor, (Func&&)func, (FuncPolicy&&)policy);
    }
    template(typename Sender, typename Func, typename FuncPolicy)
      (requires (!tag_invocable<_fn, Sender, Func, FuncPolicy>))
    auto operator()(Sender&& predecessor, Func&& func, FuncPolicy policy) const
        noexcept(std::is_nothrow_constructible_v<
          _find_if::sender<Sender, Func, FuncPolicy>, Sender, Func, FuncPolicy>)
        -> _result_t<Sender, Func, FuncPolicy> {
      return _find_if::sender<Sender, Func, FuncPolicy>{
        (Sender &&) predecessor, (Func &&) func, (FuncPolicy &&) policy};
    }
  } find_if{};
} // namespace _find_if_cpo
using _find_if_cpo::find_if;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
