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
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/execution_policy.hpp>
#include <unifex/get_execution_policy.hpp>
#include <unifex/type_list.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _bulk_tfx {

template<typename Func, typename Policy, typename Receiver>
struct _tfx_receiver {
    class type;
};

template<typename Func, typename Policy, typename Receiver>
using tfx_receiver = typename _tfx_receiver<Func, Policy, Receiver>::type;

template<typename Func, typename Policy, typename Receiver>
class _tfx_receiver<Func, Policy, Receiver>::type {
public:
    template<typename Func2, typename Receiver2>
    explicit type(Func2&& f, Policy policy, Receiver2&& r)
    : receiver_((Receiver2&&)r)
    , func_((Func2&&)f)
    , policy_(std::move(policy))
    {}

    template(typename... Values)
        (requires
            invocable<Func&, Values...> AND
            std::is_void_v<std::invoke_result_t<Func&, Values...>>)
    void set_next(Values&&... values) &
        noexcept(
            std::is_nothrow_invocable_v<Func&, Values...> &&
            is_nothrow_next_receiver_v<Receiver>) {
        std::invoke(func_, (Values&&)values...);
        unifex::set_next(receiver_);
    }

    template(typename... Values)
        (requires
            invocable<Func&, Values...> AND
            (!std::is_void_v<std::invoke_result_t<Func&, Values...>>))
    void set_next(Values&&... values) &
        noexcept(
            std::is_nothrow_invocable_v<Func&, Values...> &&
            is_nothrow_next_receiver_v<Receiver, std::invoke_result_t<Func&, Values...>>) {
        unifex::set_next(receiver_, std::invoke(func_, (Values&&)values...));
    }

    template(typename... Values)
        (requires receiver_of<Receiver, Values...>)
    void set_value(Values&&... values) noexcept(is_nothrow_receiver_of_v<Receiver, Values...>) {
        unifex::set_value(std::move(receiver_), (Values&&)values...);
    }

    template(typename Error)
        (requires receiver<Receiver, Error>)
    void set_error(Error&& error) noexcept {
        unifex::set_error(std::move(receiver_), (Error&&)error);
    }

    void set_done() noexcept {
        unifex::set_done(std::move(receiver_));
    }

    friend auto tag_invoke(tag_t<get_execution_policy>, const type& r) noexcept {
        using receiver_policy = decltype(get_execution_policy(r.receiver_));
        constexpr bool allowUnsequenced =
          is_one_of_v<receiver_policy, unsequenced_policy, parallel_unsequenced_policy> &&
          is_one_of_v<Policy, unsequenced_policy, parallel_unsequenced_policy>;
        constexpr bool allowParallel =
          is_one_of_v<receiver_policy, parallel_policy, parallel_unsequenced_policy> &&
          is_one_of_v<Policy, parallel_policy, parallel_unsequenced_policy>;

        if constexpr (allowUnsequenced && allowParallel) {
            return unifex::par_unseq;
        } else if constexpr (allowUnsequenced) {
            return unifex::unseq;
        } else if constexpr (allowParallel) {
            return unifex::par;
        } else {
            return unifex::seq;
        }
    }

    template(typename CPO, typename Self)
        (requires
            is_receiver_query_cpo_v<CPO> AND
            same_as<Self, type>)
    friend auto tag_invoke(CPO cpo, const Self& self)
        noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
        -> callable_result_t<CPO, const Receiver&> {
        return cpo(self.receiver_);
    }

private:
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    UNIFEX_NO_UNIQUE_ADDRESS Func func_;
    UNIFEX_NO_UNIQUE_ADDRESS Policy policy_;
};

template<typename Source, typename Func, typename Policy>
struct _tfx_sender {
    class type;
};

template<typename Source, typename Func, typename Policy>
using tfx_sender = typename _tfx_sender<Source, Func, Policy>::type;

template <typename Result, typename = void>
struct result_overload {
using type = type_list<Result>;
};
template <typename Result>
struct result_overload<Result, std::enable_if_t<std::is_void_v<Result>>> {
using type = type_list<>;
};

template<typename Source, typename Func, typename Policy>
class _tfx_sender<Source, Func, Policy>::type {
    template<typename... Values>
    using result = type_list<
        typename result_overload<std::invoke_result_t<Func&, Values...>>::type>;

public:
    template<
        template<typename...> class Variant,
        template<typename...> class Tuple>
    using next_types = type_list_nested_apply_t<
        typename sender_traits<Source>::template next_types<concat_type_lists_unique_t, result>,
        Variant,
        Tuple>;

    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = sender_value_types_t<Source, Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types = sender_error_types_t<Source, Variant>;

    static constexpr bool sends_done = sender_traits<Source>::sends_done;

    template<typename Source2, typename Func2>
    explicit type(Source2&& source, Func2&& func, Policy policy)
    : source_((Source2&&)source)
    , func_((Func2&&)func)
    , policy_(std::move(policy))
    {}

    template(typename Self, typename Receiver)
        (requires
            same_as<remove_cvref_t<Self>, type> AND
            constructible_from<Func, member_t<Self, Func>> AND
            receiver<Receiver> AND
            sender_to<member_t<Self, Source>, tfx_receiver<Func, Policy, remove_cvref_t<Receiver>>>)
    friend auto tag_invoke(tag_t<unifex::connect>, Self&& self, Receiver&& r)
        noexcept(
            std::is_nothrow_constructible_v<Source, member_t<Self, Source>> &&
            std::is_nothrow_constructible_v<Func, member_t<Self, Func>> &&
            std::is_nothrow_constructible_v<Policy, member_t<Self, Policy>> &&
            std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>) {
        return unifex::connect(
            static_cast<Self&&>(self).source_,
            tfx_receiver<Func, Policy, remove_cvref_t<Receiver>>{
                static_cast<Self&&>(self).func_,
                static_cast<Self&&>(self).policy_,
                static_cast<Receiver&&>(r)});
    }

private:
    UNIFEX_NO_UNIQUE_ADDRESS Source source_;
    UNIFEX_NO_UNIQUE_ADDRESS Func func_;
    UNIFEX_NO_UNIQUE_ADDRESS Policy policy_;
};

struct _fn {
    template(typename Source, typename Func, typename FuncPolicy = decltype(get_execution_policy(UNIFEX_DECLVAL(Func&))))
        (requires typed_bulk_sender<Source>)
    auto operator()(Source&& s, Func&& f) const
        noexcept(is_nothrow_callable_v<_fn, Source, Func, FuncPolicy>)
        -> callable_result_t<_fn, Source, Func, FuncPolicy> {
        return operator()((Source&&)s, (Func&&)f, get_execution_policy(f));
    }

    template(typename Source, typename Func, typename FuncPolicy)
        (requires
            typed_bulk_sender<Source> AND
            tag_invocable<_fn, Source, Func, FuncPolicy>)
    auto operator()(Source&& s, Func&& f, FuncPolicy policy) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Source, Func, FuncPolicy>)
        -> tag_invoke_result_t<_fn, Source, Func, FuncPolicy> {
        return tag_invoke(_fn{}, (Source&&)s, (Func&&)f, (FuncPolicy&&)policy);
    }

    template(typename Source, typename Func, typename FuncPolicy)
        (requires
            typed_bulk_sender<Source> AND
            (!tag_invocable<_fn, Source, Func, FuncPolicy>))
    auto operator()(Source&& s, Func&& f, FuncPolicy policy) const
        noexcept(
            std::is_nothrow_constructible_v<remove_cvref_t<Source>, Source> &&
            std::is_nothrow_constructible_v<remove_cvref_t<Func>, Func> &&
            std::is_nothrow_move_constructible_v<FuncPolicy>)
        -> tfx_sender<remove_cvref_t<Source>, remove_cvref_t<Func>, FuncPolicy> {
        return tfx_sender<remove_cvref_t<Source>, remove_cvref_t<Func>, FuncPolicy>{
            (Source&&)s, (Func&&)f, std::move(policy)
            };
    }
    template <typename Func>
    constexpr auto operator()(Func&& f) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, Func>)
        -> bind_back_result_t<_fn, Func> {
      return bind_back(*this, (Func&&)f);
    }
    template <typename Func, typename FuncPolicy>
    constexpr auto operator()(Func&& f, FuncPolicy policy) const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn, Func, FuncPolicy>)
        -> bind_back_result_t<_fn, Func, FuncPolicy> {
      return bind_back(*this, (Func&&)f, (FuncPolicy&&)policy);
    }
};

} // namespace _bulk_transform

inline constexpr _bulk_tfx::_fn bulk_transform{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
