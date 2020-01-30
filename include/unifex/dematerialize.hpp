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

namespace unifex
{
  namespace detail
  {
    template <typename Receiver>
    class dematerialize_receiver {
    public:
      template <
        typename Receiver2,
        std::enable_if_t<std::is_constructible_v<Receiver, Receiver2>, int> = 0>
      explicit dematerialize_receiver(Receiver2&& receiver) noexcept(
          std::is_nothrow_constructible_v<Receiver, Receiver2>)
        : receiver_(static_cast<Receiver2&&>(receiver)) {}

      template <
          typename CPO,
          typename... Values,
          std::enable_if_t<
              is_receiver_cpo_v<CPO> &&
                  std::is_invocable_v<CPO, Receiver, Values...>,
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
          typename... Args,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(
          CPO cpo,
          const dematerialize_receiver& r,
          Args&&... args) noexcept(std::
                                       is_nothrow_invocable_v<
                                           CPO,
                                           const Receiver&,
                                           Args...>)
          -> std::invoke_result_t<CPO, const Receiver&, Args...> {
        return static_cast<CPO&&>(cpo)(
            std::as_const(r.receiver_), static_cast<Args&&>(args)...);
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const dematerialize_receiver& r,
          Func&& func) noexcept(std::
                                    is_nothrow_invocable_v<
                                        Func&,
                                        const Receiver&>) {
        std::invoke(func, std::as_const(r.receiver_));
      }

    private:
      Receiver receiver_;
    };

    template <typename CPO, template <typename...> class Tuple>
    struct dematerialize_tuple {
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
    struct dematerialize_variant {
      template <typename... Lists>
      using apply =
          typename concat_type_lists<Lists...>::type::template apply<Variant>;
    };
  }  // namespace detail

  template <typename Source>
  class dematerialize_sender {
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
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types = typename Source::template value_types<
        detail::dematerialize_variant<Variant>::template apply,
        detail::dematerialize_tuple<tag_t<set_value>, Tuple>::template apply>;

    template <template <typename...> class Variant>
    using error_types = typename Source::template value_types<
        detail::dematerialize_variant<
            append_error_types<Variant>::template apply>::template apply,
        detail::dematerialize_tuple<tag_t<set_error>, single_type_t>::
            template apply>;

    template <typename Source2>
    explicit dematerialize_sender(Source2&& source) noexcept(
        std::is_nothrow_constructible_v<Source, Source2>)
      : source_(static_cast<Source2&&>(source)) {}

    template <
        typename Receiver,
        std::enable_if_t<
            is_connectable_v<
                Source,
                detail::dematerialize_receiver<std::decay_t<Receiver>>>,
            int> = 0>
    friend auto
    tag_invoke(tag_t<unifex::connect>, dematerialize_sender&& s, Receiver&& r)
        -> operation_t<
            Source,
            detail::dematerialize_receiver<std::decay_t<Receiver>>> {
      return unifex::connect(
          static_cast<Source&&>(s.source_),
          detail::dematerialize_receiver<std::decay_t<Receiver>>{
              static_cast<Receiver&&>(r)});
    }

    template <
        typename Receiver,
        std::enable_if_t<
            is_connectable_v<
                Source&,
                detail::dematerialize_receiver<std::decay_t<Receiver>>>,
            int> = 0>
    friend auto
    tag_invoke(tag_t<unifex::connect>, dematerialize_sender& s, Receiver&& r)
        -> operation_t<
            Source&,
            detail::dematerialize_receiver<std::decay_t<Receiver>>> {
      return unifex::connect(
          s.source_,
          detail::dematerialize_receiver<std::decay_t<Receiver>>{
              static_cast<Receiver&&>(r)});
    }

    template <
        typename Receiver,
        std::enable_if_t<
            is_connectable_v<
                const Source&,
                detail::dematerialize_receiver<std::decay_t<Receiver>>>,
            int> = 0>
    friend auto tag_invoke(
        tag_t<unifex::connect>, const dematerialize_sender& s, Receiver&& r)
        -> operation_t<
            const Source&,
            detail::dematerialize_receiver<std::decay_t<Receiver>>> {
      return unifex::connect(
          std::as_const(s.source_),
          detail::dematerialize_receiver<std::decay_t<Receiver>>{
              static_cast<Receiver&&>(r)});
    }

  private:
    Source source_;
  };

  inline constexpr struct dematerialize_cpo {
    template <
        typename Source,
        std::enable_if_t<is_tag_invocable_v<dematerialize_cpo, Source>, int> =
            0>
    auto operator()(Source&& source) const
        noexcept(is_nothrow_tag_invocable_v<dematerialize_cpo, Source>)
            -> tag_invoke_result_t<dematerialize_cpo, Source> {
      return tag_invoke(*this, static_cast<Source&&>(source));
    }

    template <
        typename Source,
        std::enable_if_t<!is_tag_invocable_v<dematerialize_cpo, Source>, int> =
            0>
    auto operator()(Source&& source) const
        noexcept(std::is_nothrow_constructible_v<
                 dematerialize_sender<std::decay_t<Source>>,
                 Source>) -> dematerialize_sender<std::decay_t<Source>> {
      return dematerialize_sender<std::decay_t<Source>>{
          static_cast<Source&&>(source)};
    }
  } dematerialize;
}  // namespace unifex
