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

#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/execution_policy.hpp>
#include <unifex/get_execution_policy.hpp>
#include <unifex/bind_back.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _bulk_join {

template<typename Receiver>
struct _join_receiver {
    class type;
};

template<typename Receiver>
using join_receiver = typename _join_receiver<Receiver>::type;

template<typename Receiver>
class _join_receiver<Receiver>::type {
public:
    template(typename Receiver2)
      (requires constructible_from<Receiver, Receiver2>)
    explicit type(Receiver2&& r) noexcept(std::is_nothrow_constructible_v<Receiver, Receiver2>)
    : receiver_((Receiver2&&)r)
    {}

    void set_next() & noexcept {}

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

    friend constexpr unifex::parallel_unsequenced_policy tag_invoke(
            tag_t<get_execution_policy>, [[maybe_unused]] const type& r) noexcept {
        return {};
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
    Receiver receiver_;
};

template<typename Source>
struct _join_sender {
    class type;
};

template<typename Source>
using join_sender = typename _join_sender<Source>::type;

template<typename Source>
class _join_sender<Source>::type {
public:
    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = sender_value_types_t<Source, Variant, Tuple>;

    template<template<typename...> class Variant>
    using error_types = sender_error_types_t<Source, Variant>;

    static constexpr bool sends_done = sender_traits<Source>::sends_done;

    template<typename Source2>
    explicit type(Source2&& s)
        noexcept(std::is_nothrow_constructible_v<Source, Source2>)
    : source_((Source2&&)s)
    {}

    template(typename Self, typename Receiver)
        (requires
            same_as<remove_cvref_t<Self>, type> AND
            sender_to<member_t<Self, Source>, join_receiver<remove_cvref_t<Receiver>>>)
    friend auto tag_invoke(tag_t<unifex::connect>, Self&& self, Receiver&& r)
        noexcept(
            std::is_nothrow_constructible_v<remove_cvref_t<Receiver>> &&
            is_nothrow_connectable_v<member_t<Self, Source>, join_receiver<remove_cvref_t<Receiver>>>)
        -> connect_result_t<member_t<Self, Source>, join_receiver<remove_cvref_t<Receiver>>>
    {
        return unifex::connect(
            static_cast<Self&&>(self).source_,
            join_receiver<remove_cvref_t<Receiver>>{static_cast<Receiver&&>(r)});
    }

private:
    Source source_;
};

struct _fn {
    template(typename Source)
        (requires
            typed_bulk_sender<Source> &&
            tag_invocable<_fn, Source>)
    auto operator()(Source&& source) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Source>)
        -> tag_invoke_result_t<_fn, Source> {
        return tag_invoke(_fn{}, (Source&&)source);
    }

    template(typename Source)
        (requires
            typed_bulk_sender<Source> &&
            (!tag_invocable<_fn, Source>))
    auto operator()(Source&& source) const
        noexcept(std::is_nothrow_constructible_v<remove_cvref_t<Source>, Source>)
        -> join_sender<remove_cvref_t<Source>> {
        return join_sender<remove_cvref_t<Source>>{
            (Source&&)source};
    }
    constexpr auto operator()() const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn>)
        -> bind_back_result_t<_fn> {
      return bind_back(*this);
    }
};

} // namespace _bulk_join

inline constexpr _bulk_join::_fn bulk_join{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
