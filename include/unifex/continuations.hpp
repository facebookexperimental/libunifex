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
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/manual_lifetime_union.hpp>

#include <type_traits>
#include <utility>
#include <cstddef>
#include <cstdio>

#include <unifex/detail/prologue.hpp>

namespace unifex {

////////////////////////////////////////////////////////////
// Continuation Handles
//
// Continuations handles represent a unit of work that is to be
// executed and that supports tail-calling to other work by returning
// another continuation to be run by the trampolining loop.
//
// A given continuation is single-shot. It can only be resumed
// or destroyed once.
//
// Continuation handles have raw-pointer semantics. This allows
// them to be efficiently copied and stored in different places.
// It is up to the caller to ensure that a given continuation
// handle is either resumed or destroyed exactly once.
//
// A continuation handle is a concept and has the following
// interface elements.
//
//   struct example_continuation
//   {
//     // Trivially copyable/destructible
//
//     // Check for nullness
//     explicit operator bool() const noexcept;
//     bool operator!() const noexcept;
//
//     // Executes this continuation.
//     // Returns another (possibly null) continuation to be executed
//     // by the trampoline loop.
//     another_continuation resume();
//
//     // Destroy the continuation.
//     // Indicates that the continuation will never be resumed.
//     void destroy() noexcept;
//
//     void swap(example_continuation&) noexcept;
//   };
//
// TODO: Should resume() methods be required to be noexcept?
//
// Executing a Continuation
// ------------------------
// Note that while when implementing a continuation type you must provide
// a 'resume()' method on your type, you should never call this method
// yourself.
//
// Instead, you resume a continuation by calling the `run_continuation()`
// helper function which excutes the whole chain of continuations until
// one of the continuations' .resume() methods returns a null continuation
// indicating that there is no more work to do. This function handles all
// of the details of transforming potentially infinite recursion into a
// trampolining loop, while still allowing statically-linear chains of
// continuations to execute sequentially.
//
// For example:
//    struct some_operation_state {
//      some_continuation start() noexcept;
//      ...
//    };
//
//    some_operation_state op;
//
//    // Execute a chain of continuations until there is no more work.
//    unifex::run_continuation(op.start());
//
// Continuation Types
// ------------------
//
// null_continuation_handle
//    Represents the terminal continuation in a chain of continuations.
//    If a continuation is default constructible then a default constructed
//    continuation is equivalent to the null continuation.
//    It is invalid to attempt to resume a null continuation handle.
//
// noop_continuation_handle
//    A valid continuation handle that represents a no-op.
//    Resuming this will do nothing and then return the null handle
//    as its continuation.
//    Return this when a continuation is required to be resumable
//    but you don't have any work to do.
//
// any_continuation_handle
//    A type-erased continuation handle.
//    All other continuation handles are implicitly convertible to this
//    continuation handle.
//    This type represents an unbounded set of possible continutation
//    types.
//
// variant_continuation_handle<Ts...>
//    A type-erased continuation handle where the set of possible
//    continuation types is known statically. Stored as a tagged
//    union of the continuation types.
//
// continuation_base<T, Derived>
//    A CRTP helper type that can be inherited from by custom
//    continuation types that are implemented as operations on an
//    operation-state object of type T.
//    This continuation handle type must always be initialised with
//    a reference to the operation-state object and can never be null.
//
// nullable_continuation_base<T, Derived>
//    Similar to continuation_base above except it also supports
//    default-construction and can hold a null continuation handle.
//    This can be used for simple conditional cases where you either
//    want to return the null continuation or 
//

struct null_continuation_handle {
    constexpr explicit operator bool() const noexcept { return false; }
    constexpr bool operator!() const noexcept { return true; }
    void resume();
    void destroy();
    void swap(null_continuation_handle& other) noexcept {}
};

template<typename Continuation>
using next_continuation_t = decltype(UNIFEX_DECLVAL(Continuation).resume());

template<typename Continuation>
inline constexpr bool is_nullable_continuation_v = std::is_default_constructible_v<Continuation>;

namespace detail
{

template<typename Continuation, typename PreviousList, typename = void>
struct run_continuation_sequential_impl;

template<typename Continuation, typename... Previous>
struct run_continuation_sequential_impl<
    Continuation,
    unifex::type_list<Previous...>,
    std::enable_if_t<unifex::is_one_of_v<Continuation, Previous...>>> {

    using result_type = Continuation;

    UNIFEX_FORCE_INLINE static Continuation invoke(Continuation h) noexcept {
        return h;
    }
};

template<typename Continuation, typename... Previous>
struct run_continuation_sequential_impl<
    Continuation,
    unifex::type_list<Previous...>,
    std::enable_if_t<!unifex::is_one_of_v<Continuation, Previous...>>> {
private:
    using next_impl = run_continuation_sequential_impl<
        next_continuation_t<Continuation>,
        unifex::type_list<Continuation, Previous...>>;

    static constexpr bool stop_here =
        is_nullable_continuation_v<next_continuation_t<Continuation>> &&
        !is_nullable_continuation_v<typename next_impl::result_type>;

public:
    using result_type = std::conditional_t<
        stop_here,
        next_continuation_t<Continuation>,
        typename next_impl::result_type>;

    UNIFEX_FORCE_INLINE static result_type invoke(Continuation h) {
        assert(h);
        if constexpr (stop_here) {
            return h.resume();
        } else if constexpr (is_nullable_continuation_v<Continuation>) {
            static_assert(is_nullable_continuation_v<result_type>);
            auto next = h.resume();
            if (next) {
                return next_impl::invoke(next);
            } else {
                return result_type{};
            }
        } else {
            return next_impl::invoke(h.resume());
        }
    }
};

template<typename Continuation>
UNIFEX_FORCE_INLINE
auto run_continuation_sequential(Continuation h) {
    //std::printf("starting sequential run at %s\n", typeid(h).name());
    return run_continuation_sequential_impl<Continuation, unifex::type_list<null_continuation_handle>>::invoke(h);
}

template<typename Continuation>
using run_continuation_sequential_result_t =
    decltype(run_continuation_sequential(std::declval<Continuation>()));

} // namespace detail

template<typename Continuation>
void run_continuation(Continuation h) {
    static_assert(
        !std::is_same_v<Continuation, null_continuation_handle>,
        "Invalid to try to run the null_continuation_handle. Use the noop_continuation_handle instead.");

    auto next = detail::run_continuation_sequential(h);

    // Check if the sequence was terminal (returns the null continuation) or cyclic.
    if constexpr (!std::is_same_v<decltype(next), null_continuation_handle>) {
        // It was cyclic. Run iterations of the cycle in a loop
        // until we get returned a continuation equivalent to the
        // null continuation.
        if (next) {
            auto next2 = detail::run_continuation_sequential(next);
            while (next2) {
                next2 = detail::run_continuation_sequential(next2);
            }
        }
    }
}

class any_continuation_handle {
public:
    using resume_fn = any_continuation_handle(void*);
    using destroy_fn = void(void*) noexcept;

    constexpr any_continuation_handle() noexcept
    : ctx_(nullptr)
    , resume_(nullptr)
    , destroy_(nullptr)
    {}

    constexpr any_continuation_handle(null_continuation_handle) noexcept
    : any_continuation_handle()
    {}

    constexpr explicit any_continuation_handle(
        void* context,
        resume_fn* resume,
        destroy_fn* destroy) noexcept
    : ctx_(context)
    , resume_(resume)
    , destroy_(destroy)
    {}

    constexpr explicit operator bool() const noexcept { return resume_ != nullptr; }
    constexpr bool operator!() const noexcept { return resume_ == nullptr; }

    UNIFEX_FORCE_INLINE any_continuation_handle resume() const {
        assert(*this);
        return resume_(ctx_);
    }

    void destroy() const noexcept {
        assert(*this);
        destroy_(ctx_);
    }

    void swap(any_continuation_handle& other) noexcept {
        std::swap(ctx_, other.ctx_);
        std::swap(resume_, other.resume_);
        std::swap(destroy_, other.destroy_);
    }

private:
    void* ctx_;
    resume_fn* resume_;
    destroy_fn* destroy_;
};

struct noop_continuation_handle {
    // Constructible from nullptr as we don't want to support
    // default construction, which requires that the resulting
    // handle would be equivalent to the null_continuation_handle.
    // Use the global constant 'noop_continuation' instead of
    // default-consructing one.
    constexpr noop_continuation_handle(std::nullptr_t) noexcept {}

    constexpr explicit operator bool() const noexcept { return true; }
    constexpr bool operator!() const noexcept { return false; }
    constexpr null_continuation_handle resume() const noexcept { return {}; }
    constexpr void destroy() const noexcept {}

    constexpr operator any_continuation_handle() const noexcept {
        return any_continuation_handle{
            nullptr,
            [](void*) noexcept -> any_continuation_handle {
                return {};
            },
            [](void*) noexcept {}
        };
    }

    void swap(noop_continuation_handle& other) noexcept {}
};

constexpr noop_continuation_handle noop_continuation{nullptr};

template<typename... Continuations>
struct _variant_continuation_handle;

namespace detail
{

template<typename Continuation>
struct flatten_variant_element {
    using type = unifex::type_list<Continuation>;
};

template<typename... Continuations>
struct flatten_variant_element<_variant_continuation_handle<Continuations...>> {
    using type = unifex::concat_type_lists_unique_t<
        typename flatten_variant_element<Continuations>::type...>;
};

template<typename... Continuations>
struct variant_or_single {
    using type = _variant_continuation_handle<Continuations...>;
};

template<typename Continuation>
struct variant_or_single<Continuation> {
    using type = Continuation;
};

template<>
struct variant_or_single<> {
    using type = null_continuation_handle;
};

} // namespace detail

template<typename... Continuations>
using variant_continuation_handle =
    typename concat_type_lists_unique_t<
        typename detail::flatten_variant_element<Continuations>::type...>
    ::template apply<detail::variant_or_single>::type;

namespace detail
{

template<typename... Continuations, typename... Previous>
struct run_continuation_sequential_impl<
    _variant_continuation_handle<Continuations...>,
    unifex::type_list<Previous...>,
    std::enable_if_t<
        !unifex::is_one_of_v<
            _variant_continuation_handle<Continuations...>,
            Previous...>
        >
    > {
private:
    template<typename Continuation>
    using next_impl_for = detail::run_continuation_sequential_impl<
        Continuation,
        unifex::type_list<
            _variant_continuation_handle<Continuations...>,
            Previous...>>;

public:
    using result_type = variant_continuation_handle<
        typename next_impl_for<Continuations>::result_type...>;

    UNIFEX_FORCE_INLINE static result_type invoke(const _variant_continuation_handle<Continuations...>& h) {
        static_assert(std::is_trivially_destructible_v<result_type>);

        // Use manual_lifetime as result_type might not be default constructible.
        unifex::manual_lifetime<result_type> result;
        h.visit([&](const auto& continuation) {
            using continuation_t = std::remove_cvref_t<decltype(continuation)>;

            result.construct_from([&] {
                return result_type{next_impl_for<continuation_t>::invoke(continuation)};
            });
        });
        return result.get();
    }
};

} // namespace detail

template<typename... Continuations>
struct _variant_continuation_handle {
private:
    static_assert(sizeof...(Continuations) >= 2);

    static_assert(
        (std::is_trivially_copy_constructible_v<Continuations> && ...),
        "All continuations in a variant must be trivially copy-constructible");
    static_assert(
        (std::is_trivially_copy_assignable_v<Continuations> && ...),
        "All continuations in a variant must be trivially copy-assignable");
    static_assert(
        (std::is_trivially_destructible_v<Continuations> && ...),
        "All continuations in a variant must be trivially destructible");

    static constexpr std::size_t first_default_constructible_index() noexcept {
        std::size_t index = 0;
        (void)(
            (std::is_default_constructible_v<Continuations> ? false : (++index, true)) || ...);
        return index;
    }

public:

    // Only default-constructible if at least one of the continuations is
    // default constructible. If so we default construct to the first such type.
    template<
        std::size_t Index = first_default_constructible_index(),
        std::enable_if_t<(Index < sizeof...(Continuations)), int> = 0>
    _variant_continuation_handle() noexcept
    : index_(Index) {
        storage_.template get<unifex::nth_type_t<Index, Continuations...>>().construct();
    }

    _variant_continuation_handle(const _variant_continuation_handle&) noexcept = default;

    template<
        typename Continuation,
        std::enable_if_t<unifex::is_one_of_v<Continuation, Continuations...>, int> = 0>
    _variant_continuation_handle(Continuation h) noexcept
    : index_(unifex::index_of_v<Continuation, Continuations...>) {
        storage_.template get<Continuation>().construct(h);
    }

    // Support converting from other variant continuation where the types
    // are a subset of the current variant's continuations.
    template<
        typename... OtherContinuations,
        std::enable_if_t<
            (unifex::is_one_of_v<OtherContinuations, Continuations...> && ...),
            int> = 0>
    _variant_continuation_handle(
        _variant_continuation_handle<OtherContinuations...> h) noexcept {
        h.visit([this](const auto& continuation) {
            *this = _variant_continuation_handle{continuation};
        });
    }

    _variant_continuation_handle& operator=(const _variant_continuation_handle& other) noexcept {
        std::memcpy(this, &other, sizeof(_variant_continuation_handle));
        return *this;
    }

    explicit operator bool() const noexcept {
        return !!*this;
    }

    bool operator!() const noexcept {
        bool result;
        visit([&](const auto& continuation) noexcept {
            result = !continuation;
        });
        return result;
    }

    operator any_continuation_handle() const noexcept {
        any_continuation_handle h;
        visit([&](const auto& continuation) noexcept {
            h = static_cast<any_continuation_handle>(continuation);
        });
        return h;
    }

    // Intentially use deduced return-type here to avoid recursive template
    // instantiation in the case where the variant type forms part of a
    // continuation cycle (eg. when representing a loop).
    auto resume() noexcept(
            (noexcept(detail::run_continuation_sequential(std::declval<Continuations>())) && ...)) {
        using resume_result_t = variant_continuation_handle<
            detail::run_continuation_sequential_result_t<Continuations>...>;

        static_assert(std::is_trivially_destructible_v<resume_result_t>);

        // Use manual_lifetime here as resume_result_t isn't necessarily default
        // constructible.
        manual_lifetime<resume_result_t> result;
        visit([&](const auto& h) {
            result.construct_from([&]() -> resume_result_t {
                return run_continuation_sequential(h);
            });
        });
        return result.get();
    }

    void destroy() noexcept {
        visit([](const auto& continuation) {
            continuation.destroy();
        });
    }

    void swap(_variant_continuation_handle& other) noexcept {
        // Don't worry about visiting.
        // All of the types should be trivially copyable.
        auto tmp = *this;
        *this = other;
        other = tmp;
    }

    template<typename F>
    UNIFEX_FORCE_INLINE void visit(F f) const
        noexcept((unifex::is_nothrow_callable_v<F, const Continuations&> && ...)) {
        visit_impl((F&&)f, std::index_sequence_for<Continuations...>{});
    }

private:

    template<typename F, std::size_t... Indices>
    UNIFEX_FORCE_INLINE void visit_impl(F f, std::index_sequence<Indices...>) const
        noexcept((unifex::is_nothrow_callable_v<F, const Continuations&> && ...)) {
        assert(index_ < sizeof...(Continuations));

        (void)((index_ == Indices
            ? ((void)f(storage_.template get<Continuations>().get()), true)
            : false) || ...);
    }

    std::size_t index_;
    unifex::manual_lifetime_union<Continuations...> storage_;
};

template<typename Op, typename Derived>
struct continuation_base {
    explicit continuation_base(Op& op) noexcept : op_(std::addressof(op)) {}

    explicit operator bool() const noexcept { return true; }
    bool operator!() const noexcept { return false; }

    operator any_continuation_handle() const noexcept {
        assert(op_ != nullptr);
        return any_continuation_handle{op_, &type_erased_resume, &type_erased_destroy};
    }

protected:
    using base_type = continuation_base;

    static any_continuation_handle type_erased_resume(void* ctx) {
        assert(ctx != nullptr);
        Derived h{*static_cast<Op*>(ctx)};
        return detail::run_continuation_sequential(h);
    }

    static void type_erased_destroy(void* ctx) noexcept {
        assert(ctx != nullptr);
        Derived h{*static_cast<Op*>(ctx)};
        h.destroy();
    }

    Op* op_;
};

template <typename Op, typename Derived>
struct nullable_continuation_base {
    nullable_continuation_base() noexcept : op_(nullptr) {}
    explicit nullable_continuation_base(Op& op) noexcept : op_(std::addressof(op)) {}

    explicit operator bool() const noexcept { return op_ != nullptr; }
    bool operator!() const noexcept { return op_ == nullptr; }

    operator any_continuation_handle() const noexcept {
        if (op_ != nullptr) {
            return any_continuation_handle{op_, &type_erased_resume, &type_erased_destroy};
        }
        return any_continuation_handle{};
    }

protected:
    static any_continuation_handle type_erased_resume(void* ctx) {
        assert(ctx != nullptr);
        Derived h{*static_cast<Op*>(ctx)};
        return detail::run_continuation_sequential(h);
    }

    static void type_erased_destroy(void* ctx) noexcept {
        assert(ctx != nullptr);
        Derived h{*static_cast<Op*>(ctx)};
        h.destroy();
    }

    using base_type = nullable_continuation_base;

    Op* op_;
};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
