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
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/bind_back.hpp>
#include <unifex/blocking.hpp>

#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _demat {
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

    template(typename CPO, typename... Values)
        (requires is_receiver_cpo_v<CPO> AND is_callable_v<CPO, Receiver, Values...>)
    void set_value(CPO cpo, Values&&... values) && noexcept(
        is_nothrow_callable_v<CPO, Receiver, Values...>) {
      static_cast<CPO&&>(cpo)(
          static_cast<Receiver&&>(receiver_),
          static_cast<Values&&>(values)...);
    }

    template(typename Error)
        (requires receiver<Receiver, Error>)
    void set_error(Error&& error) && noexcept {
      unifex::set_error(
          static_cast<Receiver&&>(receiver_), static_cast<Error&&>(error));
    }

    void set_done() && noexcept {
      unifex::set_done(static_cast<Receiver&&>(receiver_));
    }

    template(typename CPO, UNIFEX_DECLARE_NON_DEDUCED_TYPE(R, type))
        (requires is_receiver_query_cpo_v<CPO> AND is_callable_v<CPO, const Receiver&>)
    friend auto tag_invoke(CPO cpo, const UNIFEX_USE_NON_DEDUCED_TYPE(R, type)& r)
        noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
        -> callable_result_t<CPO, const Receiver&> {
      return static_cast<CPO&&>(cpo)(std::as_const(r.receiver_));
    }

  #if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
    template <typename Func>
    friend void tag_invoke(tag_t<visit_continuations>, const type& r, Func&& func) noexcept(std::
                                  is_nothrow_invocable_v<
                                      Func&,
                                      const Receiver&>) {
      std::invoke(func, std::as_const(r.receiver_));
    }
  #endif

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
  using sender = typename _sender<remove_cvref_t<Source>>::type;

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
      using apply =
          sender_error_types_t<Source, impl<Errors...>::template apply>;
    };

  public:
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types =
        sender_value_types_t<
            Source,
            variant<Variant>::template apply,
            tuple<tag_t<set_value>, Tuple>::template apply>;

    template <template <typename...> class Variant>
    using error_types =
        sender_value_types_t<
            Source,
            variant<append_error_types<Variant>::template apply>::template apply,
            tuple<tag_t<set_error>, single_type_t>::template apply>;

    static constexpr bool sends_done = sender_traits<Source>::sends_done;

    template <typename Source2>
    explicit type(Source2&& source)
        noexcept(std::is_nothrow_constructible_v<Source, Source2>)
      : source_(static_cast<Source2&&>(source)) {}

    template(typename Self, typename Receiver)
        (requires same_as<remove_cvref_t<Self>, type> AND
          sender_to<member_t<Self, Source>, receiver_t<Receiver>>)
    friend auto tag_invoke(tag_t<unifex::connect>, Self&& self, Receiver&& r)
        noexcept(is_nothrow_connectable_v<member_t<Self, Source>, receiver_t<Receiver>> &&
                 std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>)
        -> connect_result_t<member_t<Self, Source>, receiver_t<Receiver>> {
      return unifex::connect(
          static_cast<Self&&>(self).source_,
          receiver_t<Receiver>{static_cast<Receiver&&>(r)});
    }

    friend constexpr auto tag_invoke(tag_t<unifex::blocking>, const type& self) noexcept {
      return blocking(self.source_);
    }
  private:
    Source source_;
  };
} // namespace _demat

namespace _demat_cpo {
  inline const struct _fn {
  private:
    template <typename Sender>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, Sender>,
        meta_tag_invoke_result<_fn>,
        meta_quote1<_demat::sender>>::template apply<Sender>;
  public:
    template(typename Sender)
      (requires tag_invocable<_fn, Sender>)
    auto operator()(Sender&& predecessor) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Sender>)
        -> _result_t<Sender> {
      return unifex::tag_invoke(_fn{}, (Sender&&) predecessor);
    }
    template(typename Sender)
      (requires (!tag_invocable<_fn, Sender>))
    auto operator()(Sender&& predecessor) const
        noexcept(std::is_nothrow_constructible_v<remove_cvref_t<Sender>, Sender>)
        -> _result_t<Sender> {
      return _demat::sender<Sender>{(Sender &&) predecessor};
    }
    constexpr auto operator()() const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn>)
        -> bind_back_result_t<_fn> {
      return bind_back(*this);
    }
  } dematerialize{};
} // namespace _demat_cpo
using _demat_cpo::dematerialize;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
