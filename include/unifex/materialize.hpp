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
#include <unifex/type_traits.hpp>

#include <type_traits>

namespace unifex
{
  namespace detail
  {
    template <typename Receiver>
    class materialize_receiver {
    public:
      template <typename Receiver2>
      explicit materialize_receiver(Receiver2&& receiver) noexcept(
          std::is_nothrow_constructible_v<Receiver, Receiver2>)
        : receiver_(static_cast<Receiver2&&>(receiver)) {}

      template <
          typename... Values,
          std::enable_if_t<
              std::is_invocable_v<
                  decltype(unifex::set_value),
                  Receiver,
                  decltype(unifex::set_value),
                  Values...>,
              int> = 0>
      void
      set_value(Values&&... values) && noexcept(std::is_nothrow_invocable_v<
                                                decltype(unifex::set_value),
                                                Receiver,
                                                decltype(unifex::set_value),
                                                Values...>) {
        unifex::set_value(
            static_cast<Receiver&&>(receiver_),
            unifex::set_value,
            static_cast<Values&&>(values)...);
      }

      template <
          typename Error,
          std::enable_if_t<
              std::is_invocable_v<
                  decltype(unifex::set_value),
                  Receiver,
                  decltype(unifex::set_error),
                  Error>,
              int> = 0>
      void set_error(Error&& error) && noexcept {
        if constexpr (std::is_nothrow_invocable_v<
                          decltype(unifex::set_value),
                          Receiver,
                          decltype(unifex::set_error),
                          Error>) {
          unifex::set_value(
              static_cast<Receiver&&>(receiver_),
              unifex::set_error,
              static_cast<Error&&>(error));
        } else {
          try {
            unifex::set_value(
                static_cast<Receiver&&>(receiver_),
                unifex::set_error,
                static_cast<Error&&>(error));
          } catch (...) {
            unifex::set_error(
                static_cast<Receiver&&>(receiver_), std::current_exception());
          }
        }
      }

      template <
          typename... DummyPack,
          std::enable_if_t<
              sizeof...(DummyPack) == 0 &&
                  std::is_invocable_v<
                      decltype(unifex::set_value),
                      Receiver,
                      decltype(unifex::set_done)>,
              int> = 0>
      void set_done(DummyPack...) && noexcept {
        if constexpr (std::is_nothrow_invocable_v<
                          decltype(unifex::set_value),
                          Receiver,
                          decltype(unifex::set_done)>) {
          unifex::set_value(
              static_cast<Receiver&&>(receiver_), unifex::set_done);
        } else {
          try {
            unifex::set_value(
                static_cast<Receiver&&>(receiver_), unifex::set_done);
          } catch (...) {
            unifex::set_error(
                static_cast<Receiver&&>(receiver_), std::current_exception());
          }
        }
      }

      template <
          typename CPO,
          typename... Args,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(
          CPO cpo,
          const materialize_receiver& r,
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
          const materialize_receiver& r,
          Func&& func) noexcept(std::
                                    is_nothrow_invocable_v<
                                        Func&,
                                        const Receiver&>) {
        std::invoke(func, std::as_const(r.receiver_));
      }

    private:
      Receiver receiver_;
    };

    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple,
        typename... ValueTuples>
    struct materialize_error_variant {
      template <typename... Errors>
      using apply = Variant<
          ValueTuples...,
          Tuple<decltype(set_error), Errors>...,
          Tuple<decltype(set_done)>>;
    };

    template <
        typename Source,
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    struct materialize_value_types {
      template <typename... Values>
      using value_tuple = Tuple<decltype(set_value), Values...>;

      template <typename... ValueTuples>
      using value_variant = typename Source::template error_types<
          materialize_error_variant<Variant, Tuple, ValueTuples...>::
              template apply>;

      using type =
          typename Source::template value_types<value_variant, value_tuple>;
    };
  }  // namespace detail

  template <typename Source>
  class materialize_sender {
  public:
    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types =
        typename detail::materialize_value_types<Source, Variant, Tuple>::type;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    template <
        typename Source2,
        std::enable_if_t<std::is_constructible_v<Source, Source2>, int> = 0>
    explicit materialize_sender(Source2&& source) noexcept(
        std::is_nothrow_constructible_v<Source, Source2>)
      : source_(static_cast<Source2&&>(source)) {}

    template <typename Receiver>
    friend auto
    tag_invoke(tag_t<unifex::connect>, materialize_sender&& s, Receiver&& r) noexcept(
        is_nothrow_connectable_v<
            Source,
            detail::materialize_receiver<std::decay_t<Receiver>>>&&
            std::is_nothrow_constructible_v<
                detail::materialize_receiver<std::decay_t<Receiver>>,
                Receiver>)
        -> operation_t<
            Source,
            detail::materialize_receiver<std::decay_t<Receiver>>> {
      return unifex::connect(
          static_cast<Source&&>(s.source_),
          detail::materialize_receiver<std::decay_t<Receiver>>{
              static_cast<Receiver&&>(r)});
    }

    template <typename Receiver>
    friend auto
    tag_invoke(tag_t<unifex::connect>, materialize_sender& s, Receiver&& r)
        -> operation_t<
            Source&,
            detail::materialize_receiver<std::decay_t<Receiver>>> {
      return unifex::connect(
          s.source_,
          detail::materialize_receiver<std::decay_t<Receiver>>{
              static_cast<Receiver&&>(r)});
    }

    template <typename Receiver>
    friend auto tag_invoke(
        tag_t<unifex::connect>, const materialize_sender& s, Receiver&& r)
        -> operation_t<
            const Source&,
            detail::materialize_receiver<std::decay_t<Receiver>>> {
      return unifex::connect(
          std::as_const(s.source_),
          detail::materialize_receiver<std::decay_t<Receiver>>{
              static_cast<Receiver&&>(r)});
    }

  private:
    Source source_;
  };

  inline constexpr struct materialize_cpo {
    template <
        typename Source,
        std::enable_if_t<is_tag_invocable_v<materialize_cpo, Source>, int> = 0>
    auto operator()(Source&& source) const
        noexcept(is_nothrow_tag_invocable_v<materialize_cpo, Source>)
            -> tag_invoke_result_t<materialize_cpo, Source> {
      return tag_invoke(*this, static_cast<Source&&>(source));
    }

    template <
        typename Source,
        std::enable_if_t<!is_tag_invocable_v<materialize_cpo, Source>, int> = 0>
    auto operator()(Source&& source) const
        noexcept(std::is_nothrow_constructible_v<
                 materialize_sender<std::decay_t<Source>>,
                 Source>) -> materialize_sender<std::decay_t<Source>> {
      return materialize_sender<std::decay_t<Source>>{
          static_cast<Source&&>(source)};
    }
  } materialize;
}  // namespace unifex
