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

#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>

#include <tuple>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _start {
  inline constexpr struct _fn {
   private:
    template <bool>
    struct _impl {
      template <typename Operation>
      auto operator()(Operation& op) const noexcept
          -> tag_invoke_result_t<_fn, Operation&> {
        static_assert(
            is_nothrow_tag_invocable_v<_fn, Operation&>,
            "start() customisation must be noexcept");
        return unifex::tag_invoke(_fn{}, op);
      }
    };
   public:
      template <typename Operation>
      auto operator()(Operation& op) const noexcept
        -> callable_result_t<
            _impl<is_tag_invocable_v<_fn, Operation&>>, Operation&> {
      return _impl<is_tag_invocable_v<_fn, Operation&>>{}(op);
    }
  } start{};

  template <>
  struct _fn::_impl<false> {
    template <typename Operation>
    auto operator()(Operation& op) const noexcept -> decltype(op.start()) {
      static_assert(
          noexcept(op.start()),
          "start() customisation must be noexcept");
      return op.start();
    }
  };
} // namespace _start
using _start::start;

namespace _connect {
  inline constexpr struct _fn {
   private:
    template <bool>
    struct _impl {
      template <typename Sender, typename Receiver>
      auto operator()(Sender&& s, Receiver&& r) const
          noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Receiver>)
          -> tag_invoke_result_t<_fn, Sender, Receiver> {
        return unifex::tag_invoke(_fn{}, (Sender &&) s, (Receiver &&) r);
      }
    };
   public:
      template <typename Sender, typename Receiver>
      auto operator()(Sender&& s, Receiver&& r) const noexcept
        -> callable_result_t<
            _impl<is_tag_invocable_v<_fn, Sender, Receiver>>,
            Sender, Receiver> {
      return _impl<is_tag_invocable_v<_fn, Sender, Receiver>>{}(
          (Sender &&) s,
          (Receiver &&) r);
    }
  } connect{};

  template <>
  struct _fn::_impl<false> {
    template <typename Sender, typename Receiver>
    auto operator()(Sender&& s, Receiver&& r) const
        noexcept(noexcept(((Sender &&) s).connect((Receiver &&) r)))
        -> decltype(((Sender &&) s).connect((Receiver &&) r)) {
      return ((Sender &&) s).connect((Receiver &&) r);
    }
  };
} // namespace _connect
using _connect::connect;

template <typename Sender, typename Receiver>
using operation_t = decltype(connect(
    std::declval<Sender>(),
    std::declval<Receiver>()));

template <typename Sender, typename Receiver>
inline constexpr bool is_connectable_v =
  is_callable_v<decltype(connect), Sender, Receiver>;

template <typename Sender, typename Receiver>
using is_connectable = is_callable<decltype(connect), Sender, Receiver>;

template <typename Sender, typename Receiver>
inline constexpr bool is_nothrow_connectable_v =
  is_nothrow_callable_v<decltype(connect), Sender, Receiver>;

template <typename Sender, typename Receiver>
using is_nothrow_connectable = is_nothrow_callable<decltype(connect), Sender, Receiver>;

template <typename Sender, typename Adaptor>
using adapt_error_types_t =
    typename Sender::template error_types<Adaptor::template apply>;

template <
    typename Sender,
    template <typename...> class Variant,
    typename Adaptor>
using adapt_value_types_t =
    typename Sender::template value_types<Variant, typename Adaptor::apply>;

template <typename... Types>
struct single_value_type {
  using type = std::tuple<Types...>;
};

template <typename T>
struct single_value_type<T> {
  using type = T;
};

template <>
struct single_value_type<> {
  using type = void;
};

template <typename Sender>
using single_value_result_t = non_void_t<wrap_reference_t<decay_rvalue_t<
    typename Sender::template value_types<single_type, single_value_type>::
        type::type>>>;

template <typename Sender>
constexpr bool is_sender_nofail_v =
    Sender::template error_types<is_empty_list>::value;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
