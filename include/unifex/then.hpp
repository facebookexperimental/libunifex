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
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/type_list.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/bind_back.hpp>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _then {
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

template <typename Receiver, typename Func>
struct _receiver {
  struct type;
};
template <typename Receiver, typename Func>
using receiver_t = typename _receiver<Receiver, Func>::type;

template <typename Receiver, typename Func>
struct _receiver<Receiver, Func>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  template <typename... Values>
  void set_value(Values&&... values) && noexcept {
    using result_type = std::invoke_result_t<Func, Values...>;
    if constexpr (std::is_void_v<result_type>) {
      if constexpr (noexcept(std::invoke(
                        (Func &&) func_, (Values &&) values...))) {
        std::invoke((Func &&) func_, (Values &&) values...);
        unifex::set_value((Receiver &&) receiver_);
      } else {
        UNIFEX_TRY {
          std::invoke((Func &&) func_, (Values &&) values...);
          unifex::set_value((Receiver &&) receiver_);
        } UNIFEX_CATCH (...) {
          unifex::set_error((Receiver &&) receiver_, std::current_exception());
        }
      }
    } else {
      if constexpr (noexcept(std::invoke(
                        (Func &&) func_, (Values &&) values...))) {
        unifex::set_value(
            (Receiver &&) receiver_,
            std::invoke((Func &&) func_, (Values &&) values...));
      } else {
        UNIFEX_TRY {
          unifex::set_value(
              (Receiver &&) receiver_,
              std::invoke((Func &&) func_, (Values &&) values...));
        } UNIFEX_CATCH (...) {
          unifex::set_error((Receiver &&) receiver_, std::current_exception());
        }
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

template <typename Predecessor, typename Func>
struct _sender {
  struct type;
};
template <typename Predecessor, typename Func>
using sender = typename _sender<remove_cvref_t<Predecessor>, std::decay_t<Func>>::type;

template <typename Predecessor, typename Func>
struct _sender<Predecessor, Func>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

private:

  // This helper transforms an argument list into either
  // - type_list<type_list<Result>> - if Result is non-void, or
  // - type_list<type_list<>>       - if Result is void
  template <typename... Args>
  using result = type_list<
    typename detail::result_overload<std::invoke_result_t<Func, Args...>>::type>;

public:

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types =
      type_list_nested_apply_t<
          sender_value_types_t<Predecessor, concat_type_lists_unique_t, result>,
          Variant,
          Tuple>;

  template <template <typename...> class Variant>
  using error_types =
      typename concat_type_lists_unique_t<
          sender_error_types_t<Predecessor, type_list>,
          type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  template <typename Receiver>
  using receiver_t = receiver_t<Receiver, Func>;

  friend constexpr auto tag_invoke(tag_t<blocking>, const type& sender) {
    return blocking(sender.pred_);
  }

  template(typename Sender, typename Receiver)
    (requires same_as<remove_cvref_t<Sender>, type> AND receiver<Receiver> AND
        sender_to<member_t<Sender, Predecessor>, receiver_t<remove_cvref_t<Receiver>>>)
  friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r)
    noexcept(
      std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver> &&
      std::is_nothrow_constructible_v<Func, member_t<Sender, Func>> &&
      is_nothrow_connectable_v<member_t<Sender, Predecessor>, receiver_t<remove_cvref_t<Receiver>>>)
      -> connect_result_t<member_t<Sender, Predecessor>, receiver_t<remove_cvref_t<Receiver>>> {
    return unifex::connect(
      static_cast<Sender&&>(s).pred_,
      receiver_t<remove_cvref_t<Receiver>>{
        static_cast<Sender&&>(s).func_,
        static_cast<Receiver&&>(r)});
  }
};

namespace _cpo {
  struct _fn {
  private:
    template <typename Sender, typename Func>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, Sender, Func>,
        meta_tag_invoke_result<_fn>,
        meta_quote2<_then::sender>>::template apply<Sender, Func>;
  public:
    template(typename Sender, typename Func)
      (requires tag_invocable<_fn, Sender, Func>)
    auto operator()(Sender&& predecessor, Func&& func) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Func>)
        -> _result_t<Sender, Func> {
      return unifex::tag_invoke(_fn{}, (Sender&&)predecessor, (Func&&)func);
    }
    template(typename Sender, typename Func)
      (requires (!tag_invocable<_fn, Sender, Func>))
    auto operator()(Sender&& predecessor, Func&& func) const
        noexcept(std::is_nothrow_constructible_v<
          _then::sender<Sender, Func>, Sender, Func>)
        -> _result_t<Sender, Func> {
      return _then::sender<Sender, Func>{(Sender &&) predecessor, (Func &&) func};
    }
    template <typename Func>
    constexpr auto operator()(Func&& func) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, Func>)
        -> bind_back_result_t<_fn, Func> {
      return bind_back(*this, (Func &&) func);
    }
  };
} // namespace _cpo
} // namespace _then

inline constexpr _then::_cpo::_fn then {};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
