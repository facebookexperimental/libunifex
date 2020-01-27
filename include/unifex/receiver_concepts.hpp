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

#include <type_traits>

namespace unifex {
namespace _rec_cpo {
  inline constexpr struct _set_value_fn {
  private:
    template<bool>
    struct _impl {
    template <typename Receiver, typename... Values>
      auto operator()(Receiver&& r, Values&&... values) const
          noexcept(
              is_nothrow_tag_invocable_v<_set_value_fn, Receiver, Values...>)
          -> tag_invoke_result_t<_set_value_fn, Receiver, Values...> {
        return unifex::tag_invoke(
            _set_value_fn{}, (Receiver &&) r, (Values &&) values...);
      }
    };
  public:
    template <typename Receiver, typename... Values>
    auto operator()(Receiver&& r, Values&&... values) const
        noexcept(is_nothrow_callable_v<
            _impl<is_tag_invocable_v<_set_value_fn, Receiver, Values...>>,
            Receiver, Values...>)
        -> callable_result_t<
            _impl<is_tag_invocable_v<_set_value_fn, Receiver, Values...>>,
            Receiver, Values...> {
      return _impl<is_tag_invocable_v<_set_value_fn, Receiver, Values...>>{}(
          (Receiver &&) r, (Values &&) values...);
    }
  } set_value{};

  template<>
  struct _set_value_fn::_impl<false> {
    template <typename Receiver, typename... Values>
    auto operator()(Receiver&& r, Values&&... values) const
        noexcept(noexcept(
            static_cast<Receiver&&>(r).set_value((Values &&) values...)))
        -> decltype(static_cast<Receiver&&>(r).set_value((Values &&) values...)) {
      return static_cast<Receiver&&>(r).set_value((Values &&) values...);
    }
  };

  inline constexpr struct _set_next_fn {
  private:
    template<bool>
    struct _impl {
    template <typename Receiver, typename... Values>
      auto operator()(Receiver&& r, Values&&... values) const
          noexcept(
              is_nothrow_tag_invocable_v<_set_next_fn, Receiver, Values...>)
          -> tag_invoke_result_t<_set_next_fn, Receiver, Values...> {
        return unifex::tag_invoke(
            _set_next_fn{}, (Receiver &&) r, (Values &&) values...);
      }
    };
  public:
    template <typename Receiver, typename... Values>
    auto operator()(Receiver&& r, Values&&... values) const
        noexcept(std::is_nothrow_invocable_v<
            _impl<is_tag_invocable_v<_set_next_fn, Receiver, Values...>>,
            Receiver, Values...>)
        -> std::invoke_result_t<
            _impl<is_tag_invocable_v<_set_next_fn, Receiver, Values...>>,
            Receiver, Values...> {
      return _impl<is_tag_invocable_v<_set_next_fn, Receiver, Values...>>{}(
          (Receiver &&) r, (Values &&) values...);
    }
  } set_next{};

  template<>
  struct _set_next_fn::_impl<false> {
    template <typename Receiver, typename... Values>
    auto operator()(Receiver&& r, Values&&... values) const
        noexcept(noexcept(
            static_cast<Receiver&&>(r).set_next((Values &&) values...)))
        -> decltype(static_cast<Receiver&&>(r).set_next((Values &&) values...)) {
      return static_cast<Receiver&&>(r).set_next((Values &&) values...);
    }
  };

  inline constexpr struct _set_error_fn {
  private:
    template<bool>
    struct _impl {
    template <typename Receiver, typename Error>
      auto operator()(Receiver&& r, Error&& error) const noexcept
          -> tag_invoke_result_t<_set_error_fn, Receiver, Error> {
        static_assert(
            is_nothrow_tag_invocable_v<_set_error_fn, Receiver, Error>,
            "set_error() invocation is required to be noexcept.");
        static_assert(
          std::is_void_v<tag_invoke_result_t<_set_error_fn, Receiver, Error>>
        );
        return unifex::tag_invoke(
            _set_error_fn{}, (Receiver &&) r, (Error&&) error);
      }
    };
  public:
    template <typename Receiver, typename Error>
    auto operator()(Receiver&& r, Error&& error) const noexcept
        -> callable_result_t<
            _impl<is_tag_invocable_v<_set_error_fn, Receiver, Error>>,
            Receiver, Error> {
      return _impl<is_tag_invocable_v<_set_error_fn, Receiver, Error>>{}(
          (Receiver &&) r, (Error&&) error);
    }
  } set_error{};

  template<>
  struct _set_error_fn::_impl<false> {
    template <typename Receiver, typename Error>
    auto operator()(Receiver&& r, Error&& error) const noexcept
        -> decltype(static_cast<Receiver&&>(r).set_error((Error&&) error)) {
      static_assert(
          noexcept(static_cast<Receiver&&>(r).set_error((Error &&) error)),
          "receiver.set_error() method must be nothrow invocable");
      return static_cast<Receiver&&>(r).set_error((Error&&) error);
    }
  };

  inline constexpr struct _set_done_fn {
  private:
    template<bool>
    struct _impl {
    template <typename Receiver>
      auto operator()(Receiver&& r) const noexcept
          -> tag_invoke_result_t<_set_done_fn, Receiver> {
        static_assert(
            is_nothrow_tag_invocable_v<_set_done_fn, Receiver>,
            "set_done() invocation is required to be noexcept.");
        static_assert(
          std::is_void_v<tag_invoke_result_t<_set_done_fn, Receiver>>
        );
        return unifex::tag_invoke(_set_done_fn{}, (Receiver &&) r);
      }
    };
  public:
    template <typename Receiver>
    auto operator()(Receiver&& r) const noexcept
        -> callable_result_t<
            _impl<is_tag_invocable_v<_set_done_fn, Receiver>>, Receiver> {
      return _impl<is_tag_invocable_v<_set_done_fn, Receiver>>{}(
          (Receiver &&) r);
    }
  } set_done{};

  template<>
  struct _set_done_fn::_impl<false> {
    template <typename Receiver>
    auto operator()(Receiver&& r) const noexcept
        -> decltype(static_cast<Receiver&&>(r).set_done()) {
      static_assert(
          noexcept(static_cast<Receiver&&>(r).set_done()),
          "receiver.set_done() method must be nothrow invocable");
      return static_cast<Receiver&&>(r).set_done();
    }
  };
} // namespace _rec_cpo

using _rec_cpo::set_value;
using _rec_cpo::set_next;
using _rec_cpo::set_error;
using _rec_cpo::set_done;

template <typename T>
constexpr bool is_receiver_cpo_v = is_one_of_v<
    std::remove_cvref_t<T>,
    _rec_cpo::_set_value_fn,
    _rec_cpo::_set_next_fn,
    _rec_cpo::_set_error_fn,
    _rec_cpo::_set_done_fn>;

template <typename T>
using is_receiver_cpo = std::bool_constant<is_receiver_cpo_v<T>>;

} // namespace unifex
