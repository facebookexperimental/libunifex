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
#include <unifex/async_trace.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/bind_back.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  namespace _mat
  {
    template <typename Receiver>
    struct _receiver {
      class type;
    };
    template <typename Receiver>
    using receiver_t = typename _receiver<remove_cvref_t<Receiver>>::type;

    template <typename Receiver>
    class _receiver<Receiver>::type {
    public:
      template(typename Receiver2)
        (requires constructible_from<Receiver, Receiver2>)
      explicit type(Receiver2&& receiver) noexcept(
          std::is_nothrow_constructible_v<Receiver, Receiver2>)
        : receiver_(static_cast<Receiver2&&>(receiver)) {}

      template(typename... Values)
          (requires receiver_of<Receiver, decltype(unifex::set_value), Values...>)
      void
      set_value(Values&&... values) && noexcept(
          is_nothrow_receiver_of_v<Receiver, decltype(unifex::set_value), Values...>) {
        unifex::set_value(
            static_cast<Receiver&&>(receiver_),
            unifex::set_value,
            static_cast<Values&&>(values)...);
      }

      template(typename Error)
          (requires receiver_of<Receiver, decltype(unifex::set_error), Error>)
      void set_error(Error&& error) && noexcept {
        if constexpr (is_nothrow_receiver_of_v<
                          Receiver,
                          decltype(unifex::set_error),
                          Error>) {
          unifex::set_value(
              static_cast<Receiver&&>(receiver_),
              unifex::set_error,
              static_cast<Error&&>(error));
        } else {
          UNIFEX_TRY {
            unifex::set_value(
                static_cast<Receiver&&>(receiver_),
                unifex::set_error,
                static_cast<Error&&>(error));
          } UNIFEX_CATCH (...) {
            unifex::set_error(
                static_cast<Receiver&&>(receiver_), std::current_exception());
          }
        }
      }

      template(typename R = Receiver)
          (requires receiver_of<R, decltype(unifex::set_done)>)
      void set_done() && noexcept {
        if constexpr (is_nothrow_receiver_of_v<Receiver, decltype(unifex::set_done)>) {
          unifex::set_value(
              static_cast<Receiver&&>(receiver_), unifex::set_done);
        } else {
          UNIFEX_TRY {
            unifex::set_value(
                static_cast<Receiver&&>(receiver_), unifex::set_done);
          } UNIFEX_CATCH (...) {
            unifex::set_error(
                static_cast<Receiver&&>(receiver_), std::current_exception());
          }
        }
      }

      template(typename CPO, UNIFEX_DECLARE_NON_DEDUCED_TYPE(R, type))
          (requires is_receiver_query_cpo_v<CPO> AND
              is_callable_v<CPO, const Receiver&>)
      friend auto tag_invoke(
          CPO cpo,
          const UNIFEX_USE_NON_DEDUCED_TYPE(R, type)& r) noexcept(is_nothrow_callable_v<
                                           CPO,
                                           const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
        return static_cast<CPO&&>(cpo)(std::as_const(r.receiver_));
      }

      template <
          typename Func,
          UNIFEX_DECLARE_NON_DEDUCED_TYPE(CPO, tag_t<visit_continuations>),
          UNIFEX_DECLARE_NON_DEDUCED_TYPE(R, type)>
      friend void tag_invoke(
          UNIFEX_USE_NON_DEDUCED_TYPE(CPO, tag_t<visit_continuations>),
          const UNIFEX_USE_NON_DEDUCED_TYPE(R, type)& r,
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
          sender_value_types_t<Source, value_variant, value_tuple>;
    };

    template <typename Source>
    struct _sender {
      class type;
    };
    template <typename Source>
    using sender = typename _sender<remove_cvref_t<Source>>::type;

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

      static constexpr bool sends_done = false;

      template(typename Source2)
          (requires constructible_from<Source, Source2>)
      explicit type(Source2&& source) noexcept(
          std::is_nothrow_constructible_v<Source, Source2>)
        : source_(static_cast<Source2&&>(source)) {}

      template(typename Self, typename Receiver)
          (requires same_as<remove_cvref_t<Self>, type> AND
            receiver<Receiver> AND
            sender_to<member_t<Self, Source>, receiver_t<Receiver>>)
      friend auto tag_invoke(tag_t<connect>, Self&& self, Receiver&& r) noexcept(
          is_nothrow_connectable_v<member_t<Self, Source>, receiver_t<Receiver>> &&
              std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>)
          -> connect_result_t<member_t<Self, Source>, receiver_t<Receiver>> {
        return unifex::connect(
            static_cast<Self&&>(self).source_,
            receiver_t<Receiver>{static_cast<Receiver&&>(r)});
      }

    private:
      Source source_;
    };
  }  // namespace _mat

  namespace _mat_cpo {
    inline const struct _fn {
    private:
      template <typename Source>
      using _result_t =
        typename conditional_t<
          tag_invocable<_fn, Source>,
          meta_tag_invoke_result<_fn>,
          meta_quote1<_mat::sender>>::template apply<Source>;
    public:
      template(typename Source)
        (requires tag_invocable<_fn, Source>)
      auto operator()(Source&& source) const
          noexcept(is_nothrow_tag_invocable_v<_fn, Source>)
          -> _result_t<Source> {
        return unifex::tag_invoke(_fn{}, (Source&&) source);
      }
      template(typename Source)
        (requires (!tag_invocable<_fn, Source>))
      auto operator()(Source&& source) const
          noexcept(std::is_nothrow_constructible_v<_mat::sender<Source>, Source>)
          -> _result_t<Source> {
        return _mat::sender<Source>{(Source&&) source};
      }
      constexpr auto operator()() const
          noexcept(is_nothrow_callable_v<
            tag_t<bind_back>, _fn>)
          -> bind_back_result_t<_fn> {
        return bind_back(*this);
      }
    } materialize{};
  } // namespace _mat_cpo

  using _mat_cpo::materialize;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
