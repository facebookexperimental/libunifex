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
#include <unifex/async_trace.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/detail/concept_macros.hpp>

#include <type_traits>

namespace unifex
{
  namespace _mat
  {
    template <typename Receiver>
    struct _receiver {
      class type;
    };
    template <typename Receiver>
    using receiver = typename _receiver<std::remove_cvref_t<Receiver>>::type;

    template <typename Receiver>
    class _receiver<Receiver>::type {
      using receiver = type;
    public:
      template <typename Receiver2>
      explicit type(Receiver2&& receiver) noexcept(
          std::is_nothrow_constructible_v<Receiver, Receiver2>)
        : receiver_(static_cast<Receiver2&&>(receiver)) {}

      UNIFEX_TEMPLATE(typename... Values)
          (requires receiver_of<Receiver, decltype(unifex::set_value), Values...>)
      void
      set_value(Values&&... values) && noexcept(is_nothrow_callable_v<
                                                decltype(unifex::set_value),
                                                Receiver,
                                                decltype(unifex::set_value),
                                                Values...>) {
        unifex::set_value(
            static_cast<Receiver&&>(receiver_),
            unifex::set_value,
            static_cast<Values&&>(values)...);
      }

      UNIFEX_TEMPLATE(typename Error)
          (requires receiver_of<Receiver, decltype(unifex::set_error), Error>)
      void set_error(Error&& error) && noexcept {
        if constexpr (is_nothrow_callable_v<
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

      UNIFEX_TEMPLATE(typename... DummyPack)
          (requires (sizeof...(DummyPack) == 0) &&
              receiver_of<Receiver, decltype(unifex::set_done)>)
      void set_done(DummyPack...) && noexcept {
        if constexpr (is_nothrow_callable_v<
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

      UNIFEX_TEMPLATE(
          typename CPO,
          UNIFEX_DECLARE_NON_DEDUCED_TYPE(R, receiver),
          typename... Args)
          (requires (!lazy::is_true<is_receiver_cpo_v<CPO>>) &&
              lazy::callable<CPO, const Receiver&, Args...>)
      friend auto tag_invoke(
          CPO cpo,
          const UNIFEX_USE_NON_DEDUCED_TYPE(R, receiver)& r,
          Args&&... args) noexcept(is_nothrow_callable_v<
                                           CPO,
                                           const Receiver&,
                                           Args...>)
          -> callable_result_t<CPO, const Receiver&, Args...> {
        return static_cast<CPO&&>(cpo)(
            std::as_const(r.receiver_), static_cast<Args&&>(args)...);
      }

      template <
          typename Func,
          UNIFEX_DECLARE_NON_DEDUCED_TYPE(CPO, tag_t<visit_continuations>),
          UNIFEX_DECLARE_NON_DEDUCED_TYPE(R, receiver)>
      friend void tag_invoke(
          UNIFEX_USE_NON_DEDUCED_TYPE(CPO, tag_t<visit_continuations>),
          const UNIFEX_USE_NON_DEDUCED_TYPE(R, receiver)& r,
          Func&& func) noexcept(std::is_nothrow_invocable_v<
                                        Func&,
                                        const Receiver&>) {
        std::invoke(func, std::as_const(r.receiver_));
      }

    private:
      Receiver receiver_;
    };

    template <
        template <typename...> class Variant,
        template <typename...> class Tuple,
        typename... ValueTuples>
    struct error_variant {
      template <typename... Errors>
      using apply = Variant<
          ValueTuples...,
          Tuple<decltype(set_error), Errors>...,
          Tuple<decltype(set_done)>>;
    };

    template <
        typename Source,
        template <typename...> class Variant,
        template <typename...> class Tuple>
    struct value_types {
      template <typename... Values>
      using value_tuple = Tuple<decltype(set_value), Values...>;

      template <typename... ValueTuples>
      using value_variant = typename Source::template error_types<
          error_variant<Variant, Tuple, ValueTuples...>::
              template apply>;

      using type =
          typename Source::template value_types<value_variant, value_tuple>;
    };

    template <typename Source>
    struct _sender {
      class type;
    };
    template <typename Source>
    using sender = typename _sender<std::remove_cvref_t<Source>>::type;

    template <typename Source>
    class _sender<Source>::type {
      using sender = type;
    public:
      template <
          template <typename...>
          class Variant,
          template <typename...>
          class Tuple>
      using value_types =
          typename value_types<Source, Variant, Tuple>::type;

      template <template <typename...> class Variant>
      using error_types = Variant<std::exception_ptr>;

      UNIFEX_TEMPLATE(typename Source2)
          (requires std::is_constructible_v<Source, Source2>)
      explicit type(Source2&& source) noexcept(
          std::is_nothrow_constructible_v<Source, Source2>)
        : source_(static_cast<Source2&&>(source)) {}

      template <typename Receiver>
      auto connect(Receiver&& r) && noexcept(
          is_nothrow_connectable_v<Source, receiver<Receiver>> &&
              std::is_nothrow_constructible_v<std::remove_cvref_t<Receiver>, Receiver>)
          -> connect_result_t<Source, receiver<Receiver>> {
        return unifex::connect(
            static_cast<Source&&>(source_),
            receiver<Receiver>{static_cast<Receiver&&>(r)});
      }

      template <typename Receiver>
      auto connect(Receiver&& r) const &
          noexcept(
              is_nothrow_connectable_v<const Source&, receiver<Receiver>>&&
              std::is_nothrow_constructible_v<std::remove_cvref_t<Receiver>, Receiver>)
          -> connect_result_t<const Source&, receiver<Receiver>> {
        return unifex::connect(
            std::as_const(source_),
            receiver<Receiver>{static_cast<Receiver&&>(r)});
      }

    private:
      Source source_;
    };
  }  // namespace _mat

  namespace _mat_cpo {
    inline constexpr struct _fn {
    private:
      template<bool>
      struct _impl {
        template <typename Source>
        auto operator()(Source&& source) const
            noexcept(is_nothrow_tag_invocable_v<_fn, Source>) {
          return unifex::tag_invoke(_fn{}, (Source&&) source);
        }
      };
    public:
      template <typename Source>
      auto operator()(Source&& source) const
          noexcept(is_nothrow_callable_v<
            _impl<is_tag_invocable_v<_fn, Source>>, Source>) {
        return _impl<is_tag_invocable_v<_fn, Source>>{}((Source&&) source);
      }
    } materialize{};

    template<>
    struct _fn::_impl<false> {
      template <typename Source>
      auto operator()(Source&& source) const
          noexcept(std::is_nothrow_constructible_v<_mat::sender<Source>, Source>)
          -> _mat::sender<Source> {
        return _mat::sender<Source>{(Source&&) source};
      }
    };
  } // namespace _mat_cpo

  using _mat_cpo::materialize;

}  // namespace unifex
