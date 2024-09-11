/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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
#include <unifex/bind_back.hpp>
#include <unifex/blocking.hpp>
#include <unifex/continuations.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>

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
}  // namespace detail

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
      if constexpr (std::is_nothrow_invocable_v<Func, Values...>) {
        std::invoke(std::move(func_), std::forward<Values>(values)...);
        unifex::set_value(std::move(receiver_));
      } else {
        UNIFEX_TRY {
          std::invoke(std::move(func_), std::forward<Values>(values)...);
          unifex::set_value(std::move(receiver_));
        }
        UNIFEX_CATCH(...) {
          unifex::set_error(std::move(receiver_), std::current_exception());
        }
      }
    } else {
      if constexpr (std::is_nothrow_invocable_v<Func, Values...>) {
        unifex::set_value(
            std::move(receiver_),
            std::invoke(std::move(func_), std::forward<Values>(values)...));
      } else {
        UNIFEX_TRY {
          unifex::set_value(
              std::move(receiver_),
              std::invoke(std::move(func_), std::forward<Values>(values)...));
        }
        UNIFEX_CATCH(...) {
          unifex::set_error(std::move(receiver_), std::current_exception());
        }
      }
    }
  }

  template <typename Error>
  void set_error(Error&& error) && noexcept {
    unifex::set_error(std::move(receiver_), std::forward<Error>(error));
  }

  void set_done() && noexcept { unifex::set_done(std::move(receiver_)); }

  template(typename CPO, typename R)                                //
      (requires is_receiver_query_cpo_v<CPO> AND same_as<R, type>)  //
      friend auto tag_invoke(CPO cpo, const R& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Visit>
  friend void
  tag_invoke(tag_t<visit_continuations>, const type& r, Visit&& visit) {
    std::invoke(visit, r.receiver_);
  }
#endif
};

template <typename Predecessor, typename Func>
struct _sender {
  struct type;
};
template <typename Predecessor, typename Func>
using sender =
    typename _sender<remove_cvref_t<Predecessor>, std::decay_t<Func>>::type;

template <typename Predecessor, typename Func>
struct _sender<Predecessor, Func>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  instruction_ptr returnAddress_;

private:
  // This helper transforms an argument list into either
  // - type_list<type_list<Result>> - if Result is non-void, or
  // - type_list<type_list<>>       - if Result is void
  template <typename... Args>
  using result = type_list<typename detail::result_overload<
      std::invoke_result_t<Func, Args...>>::type>;

public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = type_list_nested_apply_t<
      sender_value_types_t<Predecessor, concat_type_lists_unique_t, result>,
      Variant,
      Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Predecessor, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  static constexpr auto blocking = sender_traits<Predecessor>::blocking;

  static constexpr bool is_always_scheduler_affine =
      sender_traits<Predecessor>::is_always_scheduler_affine;

  template <typename Receiver>
  using receiver_t = receiver_t<Receiver, Func>;

  template(typename Sender, typename Receiver)  //
      (requires same_as<remove_cvref_t<Sender>, type> AND receiver<Receiver> AND
           sender_to<
               member_t<Sender, Predecessor>,
               receiver_t<remove_cvref_t<Receiver>>>)  //
      friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r) noexcept(
          std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver> &&
          std::is_nothrow_constructible_v<Func, member_t<Sender, Func>> &&
          is_nothrow_connectable_v<
              member_t<Sender, Predecessor>,
              receiver_t<remove_cvref_t<Receiver>>>)
          -> connect_result_t<
              member_t<Sender, Predecessor>,
              receiver_t<remove_cvref_t<Receiver>>> {
    return unifex::connect(
        std::forward<Sender>(s).pred_,
        receiver_t<remove_cvref_t<Receiver>>{
            std::forward<Sender>(s).func_, std::forward<Receiver>(r)});
  }

  friend constexpr blocking_kind
  tag_invoke(tag_t<unifex::blocking>, const type& self) noexcept {
    return unifex::blocking(self.pred_);
  }

  friend instruction_ptr
  tag_invoke(tag_t<get_return_address>, const type& t) noexcept {
    return t.returnAddress_;
  }
};

namespace _cpo {
struct _fn {
private:
  struct _impl_fn {
    template(typename Sender, typename Func)         //
        (requires tag_invocable<_fn, Sender, Func>)  //
        auto
        operator()(Sender&& predecessor, Func&& func, instruction_ptr) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Func>)
            -> tag_invoke_result_t<_fn, Sender, Func> {
      return unifex::tag_invoke(
          _fn{}, std::forward<Sender>(predecessor), std::forward<Func>(func));
    }

    template(typename Sender, typename Func)           //
        (requires(!tag_invocable<_fn, Sender, Func>))  //
        auto
        operator()(
            Sender&& predecessor,
            Func&& func,
            instruction_ptr returnAddress) const
        noexcept(std::is_nothrow_constructible_v<
                 _then::sender<Sender, Func>,
                 Sender,
                 Func,
                 instruction_ptr>) -> _then::sender<Sender, Func> {
      return _then::sender<Sender, Func>{
          std::forward<Sender>(predecessor),
          std::forward<Func>(func),
          returnAddress};
    }
  };

public:
  template <typename Sender, typename Func>
  auto operator()(Sender&& predecessor, Func&& func) const noexcept(
      std::is_nothrow_invocable_v<_impl_fn, Sender, Func, instruction_ptr>)
      -> std::invoke_result_t<_impl_fn, Sender, Func, instruction_ptr> {
    return _impl_fn{}(
        std::forward<Sender>(predecessor),
        std::forward<Func>(func),
        instruction_ptr::read_return_address());
  }

  template <typename Func>
  constexpr auto operator()(Func&& func) const
      noexcept(std::is_nothrow_invocable_v<
               tag_t<bind_back>,
               _impl_fn,
               Func,
               instruction_ptr>)
          -> bind_back_result_t<_impl_fn, Func, instruction_ptr> {
    return bind_back(
        _impl_fn{},
        std::forward<Func>(func),
        instruction_ptr::read_return_address());
  }
};

}  // namespace _cpo
}  // namespace _then

inline constexpr _then::_cpo::_fn then{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
