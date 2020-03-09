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

#include <unifex/async_trace.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>

#include <type_traits>

namespace unifex {
namespace _demat {
  template <typename Receiver>
  struct _receiver {
    class type;
  };
  template <typename Receiver>
  using receiver = typename _receiver<std::remove_cvref_t<Receiver>>::type;

  template <typename Receiver>
  class _receiver<Receiver>::type {
   public:
    template <
      typename Receiver2,
      std::enable_if_t<std::is_constructible_v<Receiver, Receiver2>, int> = 0>
    explicit type(Receiver2&& receiver) noexcept(
        std::is_nothrow_constructible_v<Receiver, Receiver2>)
      : receiver_(static_cast<Receiver2&&>(receiver)) {}

    template <
        typename CPO,
        typename... Values,
        std::enable_if_t<
            is_receiver_cpo_v<CPO> && std::is_invocable_v<CPO, Receiver, Values...>,
            int> = 0>
    void set_value(CPO cpo, Values&&... values) && noexcept(
        std::is_nothrow_invocable_v<CPO, Receiver, Values...>) {
      static_cast<CPO&&>(cpo)(
          static_cast<Receiver&&>(receiver_),
          static_cast<Values&&>(values)...);
    }

    template <
        typename Error,
        std::enable_if_t<
            std::is_invocable_v<decltype(unifex::set_error), Receiver, Error>,
            int> = 0>
    void set_error(Error&& error) && noexcept {
      unifex::set_error(
          static_cast<Receiver&&>(receiver_), static_cast<Error&&>(error));
    }

    template <
        typename... DummyPack,
        std::enable_if_t<
            sizeof...(DummyPack) == 0 &&
                std::is_invocable_v<decltype(unifex::set_done), Receiver>,
            int> = 0>
    void set_done(DummyPack...) && noexcept {
      unifex::set_done(static_cast<Receiver&&>(receiver_));
    }

    template <
        typename CPO,
        typename R,
        typename... Args,
        std::enable_if_t<
          std::conjunction_v<
          std::negation<is_receiver_cpo<CPO>>,
          std::is_same<R, type>,
          std::is_invocable<CPO, const Receiver&, Args...>>, int> = 0>
    friend auto tag_invoke(CPO cpo, const type& r, Args&&... args)
        noexcept(std::is_nothrow_invocable_v<CPO, const Receiver&, Args...>)
        -> std::invoke_result_t<CPO, const Receiver&, Args...> {
      return static_cast<CPO&&>(cpo)(
          std::as_const(r.receiver_), static_cast<Args&&>(args)...);
    }

    template <typename Func>
    friend void tag_invoke(tag_t<visit_continuations>, const type& r, Func&& func) noexcept(std::
                                  is_nothrow_invocable_v<
                                      Func&,
                                      const Receiver&>) {
      std::invoke(func, std::as_const(r.receiver_));
    }

   private:
    Receiver receiver_;
  };

  template <typename CPO, template <typename...> class Tuple>
  struct tuple {
   private:
    template <typename... Values>
    struct apply_impl;

    template <typename First, typename... Rest>
    struct apply_impl<First, Rest...>
      : std::conditional<
            std::is_base_of_v<CPO, std::decay_t<First>>,
            type_list<Tuple<Rest...>>,
            type_list<>> {};

   public:
    template <typename... Values>
    using apply = typename apply_impl<Values...>::type;
  };

  template <template <typename...> class Variant>
  struct variant {
    template <typename... Lists>
    using apply =
        typename concat_type_lists<Lists...>::type::template apply<Variant>;
  };

  template <typename Source>
  struct _sender {
    class type;
  };
  template <typename Source>
  using sender = typename _sender<std::remove_cvref_t<Source>>::type;

  template <typename Source>
  class _sender<Source>::type {
    template <template <typename...> class Variant>
    struct append_error_types {
    private:
      template <typename... Errors>
      struct impl {
        // Concatenate and deduplicate errors from value_types, error_types along with
        // std::exception_ptr.
        template <typename... OtherErrors>
        using apply = typename concat_type_lists_unique<
            type_list<Errors...>,
            type_list<OtherErrors...>,
            type_list<std::exception_ptr>>::template apply<Variant>;
      };

    public:
      template <typename... Errors>
      using apply = typename Source::template error_types<
          impl<Errors...>::template apply>;
    };

  public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = typename Source::template value_types<
        variant<Variant>::template apply,
        tuple<tag_t<set_value>, Tuple>::template apply>;

    template <template <typename...> class Variant>
    using error_types = typename Source::template value_types<
        variant<append_error_types<Variant>::template apply>::template apply,
        tuple<tag_t<set_error>, single_type_t>::template apply>;

    template <typename Source2>
    explicit type(Source2&& source)
        noexcept(std::is_nothrow_constructible_v<Source, Source2>)
      : source_(static_cast<Source2&&>(source)) {}

    template <
        typename Receiver,
        std::enable_if_t<
            is_connectable_v<Source, receiver<Receiver>>,
            int> = 0>
    auto connect(Receiver&& r) &&
        noexcept(is_nothrow_connectable_v<Source, receiver<Receiver>> &&
                 std::is_nothrow_constructible_v<std::remove_cvref_t<Receiver>, Receiver>)
        -> operation_t<Source, receiver<Receiver>> {
      return unifex::connect(
          static_cast<Source&&>(source_),
          receiver<Receiver>{static_cast<Receiver&&>(r)});
    }

    template <
        typename Receiver,
        std::enable_if_t<
            is_connectable_v<Source&, receiver<Receiver>>,
            int> = 0>
    auto connect(Receiver&& r) &
        noexcept(is_nothrow_connectable_v<Source&, receiver<Receiver>> &&
                 std::is_nothrow_constructible_v<std::remove_cvref_t<Receiver>, Receiver>)
        -> operation_t<Source&, receiver<Receiver>> {
      return unifex::connect(
          source_,
          receiver<Receiver>{static_cast<Receiver&&>(r)});
    }

    template <
        typename Receiver,
        std::enable_if_t<
            is_connectable_v<const Source&, receiver<Receiver>>,
            int> = 0>
    auto connect(Receiver&& r) const &
        noexcept(is_nothrow_connectable_v<const Source&, receiver<Receiver>> &&
                 std::is_nothrow_constructible_v<std::remove_cvref_t<Receiver>, Receiver>)
        -> operation_t<const Source&, receiver<Receiver>> {
      return unifex::connect(
          std::as_const(source_),
          receiver<Receiver>{static_cast<Receiver&&>(r)});
    }

  private:
    Source source_;
  };
} // namespace _demat

namespace _demat_cpo {
  inline constexpr struct _fn {
   private:
    template<bool>
    struct _impl {
      template <typename Sender>
      auto operator()(Sender&& predecessor) const
          noexcept(is_nothrow_tag_invocable_v<_fn, Sender>) {
        return unifex::tag_invoke(_fn{}, (Sender&&) predecessor);
      }
    };
   public:
    template <typename Sender>
    auto operator()(Sender&& predecessor) const
        noexcept(std::is_nothrow_invocable_v<
          _impl<is_tag_invocable_v<_fn, Sender>>, Sender>) {
      return _impl<is_tag_invocable_v<_fn, Sender>>{}((Sender&&) predecessor);
    }
  } dematerialize{};

  template<>
  struct _fn::_impl<false> {
    template <typename Sender>
    auto operator()(Sender&& predecessor) const
        noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Sender>, Sender>)
        -> _demat::sender<Sender> {
      return _demat::sender<Sender>{(Sender &&) predecessor};
    }
  };
} // namespace _demat_cpo
using _demat_cpo::dematerialize;

}  // namespace unifex
