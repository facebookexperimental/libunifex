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

#include <type_traits>

#if !defined(UNIFEX_CXX_CONCEPTS)
#ifdef UNIFEX_DOXYGEN_INVOKED
#define UNIFEX_CXX_CONCEPTS 201800L
#elif defined(__cpp_concepts) && __cpp_concepts > 0
#define UNIFEX_CXX_CONCEPTS __cpp_concepts
#else
#define UNIFEX_CXX_CONCEPTS 0L
#endif
#endif

#ifndef UNIFEX_CXX_INLINE_VARIABLES
#ifdef __cpp_inline_variables // TODO: fix this if SD-6 picks another name
#define UNIFEX_CXX_INLINE_VARIABLES __cpp_inline_variables
// TODO: remove once clang defines __cpp_inline_variables (or equivalent)
#elif defined(__clang__) && \
    (__clang_major__ > 3 || __clang_major__ == 3 && __clang_minor__ == 9) && \
    __cplusplus > 201402L
#define UNIFEX_CXX_INLINE_VARIABLES 201606L
#else
#define UNIFEX_CXX_INLINE_VARIABLES __cplusplus
#endif  // __cpp_inline_variables
#endif  // UNIFEX_CXX_INLINE_VARIABLES

#if UNIFEX_CXX_INLINE_VARIABLES < 201606L
#define UNIFEX_INLINE_VAR
#else  // UNIFEX_CXX_INLINE_VARIABLES >= 201606L
#define UNIFEX_INLINE_VAR inline
#endif // UNIFEX_CXX_INLINE_VARIABLES

#if (defined(__cpp_lib_type_trait_variable_templates) && \
    __cpp_lib_type_trait_variable_templates > 0)
#define UNIFEX_CXX_TRAIT_VARIABLE_TEMPLATES 1
#else
#define UNIFEX_CXX_TRAIT_VARIABLE_TEMPLATES 0
#endif

#if defined(__clang__)
#define UNIFEX_IS_SAME(...) __is_same(__VA_ARGS__)
#elif defined(__GNUC__) && __GNUC__ >= 6
#define UNIFEX_IS_SAME(...) __is_same_as(__VA_ARGS__)
#elif UNIFEX_CXX_TRAIT_VARIABLE_TEMPLATES
#define UNIFEX_IS_SAME(...) std::is_same_v<__VA_ARGS__>
#else
#define UNIFEX_IS_SAME(...) std::is_same<__VA_ARGS__>::value
#endif

#if defined(__GNUC__) || defined(_MSC_VER)
#define UNIFEX_IS_BASE_OF(...) __is_base_of(__VA_ARGS__)
#elif UNIFEX_CXX_TRAIT_VARIABLE_TEMPLATES
#define UNIFEX_IS_BASE_OF(...) std::is_base_of_v<__VA_ARGS__>
#else
#define UNIFEX_IS_BASE_OF(...) std::is_base_of<__VA_ARGS__>::value
#endif

#if defined(__clang__) || defined(_MSC_VER) || \
    (defined(__GNUC__) && __GNUC__ >= 8)
#define UNIFEX_IS_CONSTRUCTIBLE(...) __is_constructible(__VA_ARGS__)
#elif UNIFEX_CXX_TRAIT_VARIABLE_TEMPLATES
#define UNIFEX_IS_CONSTRUCTIBLE(...) std::is_constructible_v<__VA_ARGS__>
#else
#define UNIFEX_IS_CONSTRUCTIBLE(...) std::is_constructible<__VA_ARGS__>::value
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define UNIFEX_WORKAROUND_MSVC_779763 // FATAL_UNREACHABLE calling constexpr function via template parameter
#define UNIFEX_WORKAROUND_MSVC_780775 // Incorrect substitution in function template return type
#define UNIFEX_WORKAROUND_MSVC_654601 // Failure to invoke *implicit* bool conversion in a constant expression
#endif

#define UNIFEX_PP_CAT_(X, ...)  X ## __VA_ARGS__
#define UNIFEX_PP_CAT(X, ...)   UNIFEX_PP_CAT_(X, __VA_ARGS__)

#define UNIFEX_PP_CAT2_(X, ...)  X ## __VA_ARGS__
#define UNIFEX_PP_CAT2(X, ...)   UNIFEX_PP_CAT2_(X, __VA_ARGS__)

#define UNIFEX_PP_CAT3_(X, ...)  X ## __VA_ARGS__
#define UNIFEX_PP_CAT3(X, ...)   UNIFEX_PP_CAT3_(X, __VA_ARGS__)

#define UNIFEX_PP_CAT4_(X, ...)  X ## __VA_ARGS__
#define UNIFEX_PP_CAT4(X, ...)   UNIFEX_PP_CAT4_(X, __VA_ARGS__)

#define UNIFEX_PP_EVAL_(X, ARGS) X ARGS
#define UNIFEX_PP_EVAL(X, ...) UNIFEX_PP_EVAL_(X, (__VA_ARGS__))

#define UNIFEX_PP_EVAL2_(X, ARGS) X ARGS
#define UNIFEX_PP_EVAL2(X, ...) UNIFEX_PP_EVAL2_(X, (__VA_ARGS__))

#define UNIFEX_PP_EXPAND(...) __VA_ARGS__
#define UNIFEX_PP_EAT(...)

#define UNIFEX_PP_CHECK(...) UNIFEX_PP_EXPAND(UNIFEX_PP_CHECK_N(__VA_ARGS__, 0,))
#define UNIFEX_PP_CHECK_N(x, n, ...) n
#define UNIFEX_PP_PROBE(x) x, 1,
#define UNIFEX_PP_PROBE_N(x, n) x, n,

#define UNIFEX_PP_IS_PAREN(x) UNIFEX_PP_CHECK(UNIFEX_PP_IS_PAREN_PROBE x)
#define UNIFEX_PP_IS_PAREN_PROBE(...) UNIFEX_PP_PROBE(~)

// UNIFEX_CXX_VA_OPT
#ifndef UNIFEX_CXX_VA_OPT
#if __cplusplus > 201703L
#define UNIFEX_CXX_VA_OPT_(...) UNIFEX_PP_CHECK(__VA_OPT__(,) 1)
#define UNIFEX_CXX_VA_OPT UNIFEX_CXX_VA_OPT_(~)
#else
#define UNIFEX_CXX_VA_OPT 0
#endif
#endif // UNIFEX_CXX_VA_OPT

// The final UNIFEX_PP_EXPAND here is to avoid
// https://stackoverflow.com/questions/5134523/msvc-doesnt-expand-va-args-correctly
#define UNIFEX_PP_COUNT(...)                                                    \
    UNIFEX_PP_EXPAND(UNIFEX_PP_COUNT_(__VA_ARGS__,                              \
        50, 49, 48, 47, 46, 45, 44, 43, 42, 41,                                 \
        40, 39, 38, 37, 36, 35, 34, 33, 32, 31,                                 \
        30, 29, 28, 27, 26, 25, 24, 23, 22, 21,                                 \
        20, 19, 18, 17, 16, 15, 14, 13, 12, 11,                                 \
        10, 9, 8, 7, 6, 5, 4, 3, 2, 1,))                                        \
    /**/
#define UNIFEX_PP_COUNT_(                                                       \
    _01, _02, _03, _04, _05, _06, _07, _08, _09, _10,                           \
    _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,                           \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30,                           \
    _31, _32, _33, _34, _35, _36, _37, _38, _39, _40,                           \
    _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, N, ...)                   \
    N                                                                           \
    /**/

#define UNIFEX_PP_IIF(BIT) UNIFEX_PP_CAT_(UNIFEX_PP_IIF_, BIT)
#define UNIFEX_PP_IIF_0(TRUE, ...) __VA_ARGS__
#define UNIFEX_PP_IIF_1(TRUE, ...) TRUE

#define UNIFEX_PP_LPAREN (

#define UNIFEX_PP_NOT(BIT) UNIFEX_PP_CAT_(UNIFEX_PP_NOT_, BIT)
#define UNIFEX_PP_NOT_0 1
#define UNIFEX_PP_NOT_1 0

#define UNIFEX_PP_EMPTY()
#define UNIFEX_PP_COMMA() ,
#define UNIFEX_PP_LBRACE() {
#define UNIFEX_PP_RBRACE() }
#define UNIFEX_PP_COMMA_IIF(X)                                                     \
    UNIFEX_PP_IIF(X)(UNIFEX_PP_EMPTY, UNIFEX_PP_COMMA)()                                 \
    /**/

#define UNIFEX_PP_FOR_EACH(M, ...) \
    UNIFEX_PP_FOR_EACH_N(UNIFEX_PP_COUNT(__VA_ARGS__), M, __VA_ARGS__)
#define UNIFEX_PP_FOR_EACH_N(N, M, ...) \
    UNIFEX_PP_CAT2(UNIFEX_PP_FOR_EACH_, N)(M, __VA_ARGS__)
#define UNIFEX_PP_FOR_EACH_1(M, _1) \
    M(_1)
#define UNIFEX_PP_FOR_EACH_2(M, _1, _2) \
    M(_1) M(_2)
#define UNIFEX_PP_FOR_EACH_3(M, _1, _2, _3) \
    M(_1) M(_2) M(_3)
#define UNIFEX_PP_FOR_EACH_4(M, _1, _2, _3, _4) \
    M(_1) M(_2) M(_3) M(_4)
#define UNIFEX_PP_FOR_EACH_5(M, _1, _2, _3, _4, _5) \
    M(_1) M(_2) M(_3) M(_4) M(_5)
#define UNIFEX_PP_FOR_EACH_6(M, _1, _2, _3, _4, _5, _6) \
    M(_1) M(_2) M(_3) M(_4) M(_5) M(_6)
#define UNIFEX_PP_FOR_EACH_7(M, _1, _2, _3, _4, _5, _6, _7) \
    M(_1) M(_2) M(_3) M(_4) M(_5) M(_6) M(_7)
#define UNIFEX_PP_FOR_EACH_8(M, _1, _2, _3, _4, _5, _6, _7, _8) \
    M(_1) M(_2) M(_3) M(_4) M(_5) M(_6) M(_7) M(_8)

#define UNIFEX_PP_PROBE_EMPTY_PROBE_UNIFEX_PP_PROBE_EMPTY                             \
    UNIFEX_PP_PROBE(~)                                                             \

#define UNIFEX_PP_PROBE_EMPTY()
#define UNIFEX_PP_IS_NOT_EMPTY(...)                                                \
    UNIFEX_PP_EVAL(                                                                \
        UNIFEX_PP_CHECK,                                                           \
        UNIFEX_PP_CAT(                                                             \
            UNIFEX_PP_PROBE_EMPTY_PROBE_,                                          \
            UNIFEX_PP_PROBE_EMPTY __VA_ARGS__ ()))                                 \
    /**/

#define UNIFEX_CONCEPT_FRAGMENT_REQS_M0(REQ) \
    UNIFEX_CONCEPT_FRAGMENT_REQS_SELECT_(REQ)(REQ)
#define UNIFEX_CONCEPT_FRAGMENT_REQS_M1(REQ) UNIFEX_PP_EXPAND REQ
#define UNIFEX_CONCEPT_FRAGMENT_REQS_(...) \
    { UNIFEX_PP_FOR_EACH(UNIFEX_CONCEPT_FRAGMENT_REQS_M, __VA_ARGS__) }
#define UNIFEX_CONCEPT_FRAGMENT_REQS_SELECT_(REQ)                          \
    UNIFEX_PP_CAT3(UNIFEX_CONCEPT_FRAGMENT_REQS_SELECT_,                                     \
        UNIFEX_PP_EVAL(UNIFEX_PP_CHECK, UNIFEX_PP_CAT3(                                   \
            UNIFEX_CONCEPT_FRAGMENT_REQS_SELECT_PROBE_, \
            REQ)))                   \
    /**/
#define UNIFEX_CONCEPT_FRAGMENT_REQS_SELECT_PROBE_requires UNIFEX_PP_PROBE_N(~, 1)
#define UNIFEX_CONCEPT_FRAGMENT_REQS_SELECT_PROBE_noexcept UNIFEX_PP_PROBE_N(~, 2)

#define UNIFEX_CONCEPT_FRAGMENT_REQS_SELECT_0 UNIFEX_PP_EXPAND
#define UNIFEX_CONCEPT_FRAGMENT_REQS_SELECT_1 UNIFEX_CONCEPT_FRAGMENT_REQS_REQUIRES_OR_NOEXCEPT
#define UNIFEX_CONCEPT_FRAGMENT_REQS_SELECT_2 UNIFEX_CONCEPT_FRAGMENT_REQS_REQUIRES_OR_NOEXCEPT
#define UNIFEX_CONCEPT_FRAGMENT_REQS_REQUIRES_OR_NOEXCEPT(REQ) \
    UNIFEX_PP_CAT4(\
        UNIFEX_CONCEPT_FRAGMENT_REQS_REQUIRES_,\
        REQ)

#if UNIFEX_CXX_CONCEPTS || defined(UNIFEX_DOXYGEN_INVOKED)

    #define UNIFEX_CONCEPT concept

    #define UNIFEX_CONCEPT_FRAGMENT(NAME, ...) \
        concept NAME = UNIFEX_PP_CAT(UNIFEX_CONCEPT_FRAGMENT_REQS_, __VA_ARGS__)
    #define UNIFEX_CONCEPT_FRAGMENT_REQS_requires(...) \
        requires(__VA_ARGS__) UNIFEX_CONCEPT_FRAGMENT_REQS_
    #define UNIFEX_CONCEPT_FRAGMENT_REQS_M(REQ) \
        UNIFEX_PP_CAT2(UNIFEX_CONCEPT_FRAGMENT_REQS_M, UNIFEX_PP_IS_PAREN(REQ))(REQ);
    #define UNIFEX_CONCEPT_FRAGMENT_REQS_REQUIRES_requires(...) \
        requires __VA_ARGS__
    #define UNIFEX_CONCEPT_FRAGMENT_REQS_REQUIRES_noexcept(...) \
        { __VA_ARGS__ } noexcept

    #define UNIFEX_FRAGMENT(NAME, ...) \
        NAME<__VA_ARGS__>

#else

    #define UNIFEX_CONCEPT UNIFEX_INLINE_VAR constexpr bool

    #define UNIFEX_CONCEPT_FRAGMENT(NAME, ...) \
        auto NAME ## UNIFEX_CONCEPT_FRAGMENT_impl_ \
            UNIFEX_CONCEPT_FRAGMENT_REQS_ ## __VA_ARGS__> {} \
        template<typename... As>\
        char NAME ## UNIFEX_CONCEPT_FRAGMENT_(\
            ::unifex::_concept::tag<As...> *, \
            decltype(&NAME ## UNIFEX_CONCEPT_FRAGMENT_impl_<As...>)); \
        char (&NAME ## UNIFEX_CONCEPT_FRAGMENT_(...))[2] \
        /**/
    #define M(ARG) ARG,
    #if defined(_MSC_VER) && !defined(__clang__)
        #define UNIFEX_CONCEPT_FRAGMENT_TRUE(...) \
            ::unifex::_concept::true_<decltype(\
                UNIFEX_PP_FOR_EACH(UNIFEX_CONCEPT_FRAGMENT_REQS_M, __VA_ARGS__)\
                void())>()
    #else
        #define UNIFEX_CONCEPT_FRAGMENT_TRUE(...) \
            !(decltype(UNIFEX_PP_FOR_EACH(UNIFEX_CONCEPT_FRAGMENT_REQS_M, __VA_ARGS__)\
            void(),\
            false){})
    #endif
    #define UNIFEX_CONCEPT_FRAGMENT_REQS_requires(...) \
        (__VA_ARGS__) -> std::enable_if_t<UNIFEX_CONCEPT_FRAGMENT_REQS_2_
    #define UNIFEX_CONCEPT_FRAGMENT_REQS_2_(...) \
        UNIFEX_CONCEPT_FRAGMENT_TRUE(__VA_ARGS__)
    #define UNIFEX_CONCEPT_FRAGMENT_REQS_M(REQ) \
        UNIFEX_PP_CAT2(UNIFEX_CONCEPT_FRAGMENT_REQS_M, UNIFEX_PP_IS_PAREN(REQ))(REQ),
    #define UNIFEX_CONCEPT_FRAGMENT_REQS_REQUIRES_requires(...) \
        ::unifex::requires_<__VA_ARGS__>
    #define UNIFEX_CONCEPT_FRAGMENT_REQS_REQUIRES_noexcept(...) \
        ::unifex::requires_<noexcept(__VA_ARGS__)>

    #define UNIFEX_FRAGMENT(NAME, ...) \
        (1u==sizeof(NAME ## UNIFEX_CONCEPT_FRAGMENT_(\
            static_cast<::unifex::_concept::tag<__VA_ARGS__> *>(nullptr), nullptr))) \
        /**/

#endif

#ifdef UNIFEX_WORKAROUND_MSVC_654601
    #define UNIFEX_FORCE_TO_BOOL static_cast<bool>
#else
    #define UNIFEX_FORCE_TO_BOOL
#endif

////////////////////////////////////////////////////////////////////////////////
// UNIFEX_TEMPLATE
// Usage:
//   UNIFEX_TEMPLATE(typename A, typename B)
//     (requires Concept1<A> && Concept2<B>)
//   void foo(A a, B b)
//   {}
#if UNIFEX_CXX_CONCEPTS
    #define UNIFEX_TEMPLATE(...)                                                       \
        template<__VA_ARGS__> UNIFEX_PP_EXPAND                                         \
        /**/
    #define UNIFEX_TEMPLATE_DEF UNIFEX_TEMPLATE                                           \
        /**/
#else
    #define UNIFEX_TEMPLATE                                                            \
        UNIFEX_TEMPLATE_SFINAE                                                         \
        /**/
    #define UNIFEX_TEMPLATE_DEF UNIFEX_TEMPLATE_DEF_SFINAE                                \
        /**/
#endif

#define UNIFEX_TEMPLATE_SFINAE(...)                                                \
    template<__VA_ARGS__ UNIFEX_TEMPLATE_SFINAE_AUX_                               \
    /**/
#define UNIFEX_TEMPLATE_SFINAE_AUX_(...) ,                                         \
    typename UNIFEX_true_ = std::true_type,                                        \
    std::enable_if_t<                                                           \
        UNIFEX_FORCE_TO_BOOL(                                                      \
            UNIFEX_PP_CAT(UNIFEX_TEMPLATE_SFINAE_AUX_3_, __VA_ARGS__) && UNIFEX_true_{}),\
        int> = 0>                                                               \
    /**/
#define UNIFEX_TEMPLATE_DEF_SFINAE(...)                                            \
    template<__VA_ARGS__ UNIFEX_TEMPLATE_DEF_SFINAE_AUX_                           \
    /**/
#define UNIFEX_TEMPLATE_DEF_SFINAE_AUX_(...) ,                                     \
    typename UNIFEX_true_,                                                         \
    std::enable_if_t<                                                           \
        UNIFEX_FORCE_TO_BOOL(                                                      \
            UNIFEX_PP_CAT(UNIFEX_TEMPLATE_SFINAE_AUX_3_, __VA_ARGS__) && UNIFEX_true_{}),\
        int>>                                                                   \
    /**/
#define UNIFEX_TEMPLATE_SFINAE_AUX_3_requires

namespace unifex {
namespace _concept {

template<typename...>
struct tag;

template<class>
constexpr bool true_()
{
    return true;
}

} // namespace _concept

#if defined(__clang__) || defined(_MSC_VER)
template<bool B>
std::enable_if_t<B> requires_()
{}
#else
template<bool B>
UNIFEX_INLINE_VAR constexpr std::enable_if_t<B, int> requires_ = 0;
#endif

template<bool B>
UNIFEX_CONCEPT _is_true = B;

} // namespace unifex
