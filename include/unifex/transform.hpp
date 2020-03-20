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
#include <unifex/type_list.hpp>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace unifex {
namespace _tfx {
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
using receiver = typename _receiver<Receiver, Func>::type;

template <typename Receiver, typename Func>
struct _receiver<Receiver, Func>::type {
  using receiver = type;
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
        try {
          std::invoke((Func &&) func_, (Values &&) values...);
          unifex::set_value((Receiver &&) receiver_);
        } catch (...) {
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
        try {
          unifex::set_value(
              (Receiver &&) receiver_,
              std::invoke((Func &&) func_, (Values &&) values...));
        } catch (...) {
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

  template <
      typename CPO,
      typename R,
      typename... Args,
      std::enable_if_t<!is_receiver_cpo_v<CPO> && std::is_same_v<R, receiver>, int> = 0>
  friend auto tag_invoke(CPO cpo, const R& r, Args&&... args) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&, Args...>)
      -> callable_result_t<CPO, const Receiver&, Args...> {
    return std::move(cpo)(std::as_const(r.receiver_), static_cast<Args&&>(args)...);
  }

  template <typename Visit>
  friend void tag_invoke(tag_t<visit_continuations>, const receiver& r, Visit&& visit) {
    std::invoke(visit, r.receiver_);
  }
};

template <typename Predecessor, typename Func>
struct _sender {
  struct type;
};
template <typename Predecessor, typename Func>
using sender = typename _sender<std::remove_cvref_t<Predecessor>, std::decay_t<Func>>::type;

template <typename Predecessor, typename Func>
struct _sender<Predecessor, Func>::type {
  using sender = type;
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

private:

  // This helper transforms an argument list into either
  // - type_list<type_list<Result>> - if Result is non-void, or
  // - type_list<type_list<>>       - if Result is void
  template<typename... Args>
  using result = type_list<
    typename detail::result_overload<std::invoke_result_t<Func, Args...>>::type>;

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
  using receiver = receiver<Receiver, Func>;

  friend constexpr auto tag_invoke(tag_t<blocking>, const sender& sender) {
    return blocking(sender.pred_);
  }

  template <typename Receiver>
  auto connect(Receiver&& r) &&
      noexcept(is_nothrow_tag_invocable_v<tag_t<unifex::connect>, Predecessor, receiver<std::remove_cvref_t<Receiver>>>)
      -> operation_t<Predecessor, receiver<std::remove_cvref_t<Receiver>>> {
    return unifex::connect(
        std::forward<Predecessor>(pred_),
        receiver<std::remove_cvref_t<Receiver>>{
            std::forward<Func>(func_), std::forward<Receiver>(r)});
  }

  template <typename Receiver>
  auto connect(Receiver&& r) &
      noexcept(is_nothrow_tag_invocable_v<tag_t<unifex::connect>, Predecessor&, receiver<std::remove_cvref_t<Receiver>>>)
      -> operation_t<Predecessor&, receiver<std::remove_cvref_t<Receiver>>>{
    return unifex::connect(
        pred_,
        receiver<std::remove_cvref_t<Receiver>>{func_, std::forward<Receiver>(r)});
  }

  template <typename Receiver>
  auto connect(Receiver&& r) const &
      noexcept(is_nothrow_tag_invocable_v<tag_t<unifex::connect>, const Predecessor&, receiver<std::remove_cvref_t<Receiver>>>)
      -> operation_t<const Predecessor&, receiver<std::remove_cvref_t<Receiver>>> {
    return unifex::connect(
        pred_,
        receiver<std::remove_cvref_t<Receiver>>{func_, std::forward<Receiver>(r)});
  }
};
} // namespace _tfx

namespace _tfx_cpo {
  inline constexpr struct _fn {
  private:
    template<bool>
    struct _impl {
      template <typename Sender, typename Func>
      auto operator()(Sender&& predecessor, Func&& func) const
          noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Func>)
          -> tag_invoke_result_t<_fn, Sender, Func> {
        return unifex::tag_invoke(_fn{}, (Sender&&)predecessor, (Func&&)func);
      }
    };
  public:
    template <typename Sender, typename Func>
    auto operator()(Sender&& predecessor, Func&& func) const
        noexcept(is_nothrow_callable_v<
            _impl<is_tag_invocable_v<_fn, Sender, Func>>, Sender, Func>)
        -> callable_result_t<
            _impl<is_tag_invocable_v<_fn, Sender, Func>>, Sender, Func> {
      return _impl<is_tag_invocable_v<_fn, Sender, Func>>{}(
        (Sender&&)predecessor, (Func&&)func);
    }
  } transform{};

  template<>
  struct _fn::_impl<false> {
    template <typename Sender, typename Func>
    auto operator()(Sender&& predecessor, Func&& func) const
        noexcept(std::is_nothrow_constructible_v<
          _tfx::sender<Sender, Func>, Sender, Func>)
        -> _tfx::sender<Sender, Func> {
      return _tfx::sender<Sender, Func>{(Sender &&) predecessor, (Func &&) func};
    }
  };
} // namespace _tfx_cpo
using _tfx_cpo::transform;
} // namespace unifex
