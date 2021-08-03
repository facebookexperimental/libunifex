/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _create {

template <typename Receiver, typename Fn, typename Context>
struct _op {
  struct type {
    explicit type(Receiver rec, Fn fn, Context ctx)
        noexcept(std::is_nothrow_move_constructible_v<Receiver> &&
          std::is_nothrow_move_constructible_v<Fn> &&
          std::is_nothrow_move_constructible_v<Context>)
      : rec_((Receiver&&) rec), fn_((Fn&&) fn), ctx_((Context&&) ctx) {}

    template (typename... Ts)
      (requires receiver_of<Receiver, Ts...>)
    void set_value(Ts&&... ts) noexcept(is_nothrow_receiver_of_v<Receiver, Ts...>) {
      unifex::set_value((Receiver&&) rec_, (Ts&&) ts...);
    }

    template (typename Error)
      (requires receiver<Receiver, Error>)
    void set_error(Error&& error) noexcept {
      unifex::set_error((Receiver&&) rec_, (Error&&) error);
    }

    void set_done() noexcept {
      unifex::set_done((Receiver&&) rec_);
    }

    template (class Ctx = Context)
      (requires (!same_as<Ctx, detail::_empty<>>))
    Context const& context() const & noexcept { return ctx_; }

    template (class Ctx = Context)
      (requires (!same_as<Ctx, detail::_empty<>>))
    Context&& context() && noexcept { return (Context&&) ctx_; }

  private:
    friend void tag_invoke(tag_t<start>, type& self) noexcept try {
      self.fn_(self);
    } catch(...) {
      unifex::set_error((Receiver&&) self.rec_, std::current_exception());
    }

    // Forward other receiver queries
    template (typename CPO)
      (requires is_receiver_query_cpo_v<CPO> AND is_callable_v<CPO, const Receiver&>)
    friend auto tag_invoke(CPO cpo, const type& self)
        noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
      return std::move(cpo)(self.rec_);
    }

    UNIFEX_NO_UNIQUE_ADDRESS Receiver rec_;
    UNIFEX_NO_UNIQUE_ADDRESS Fn fn_;
    UNIFEX_NO_UNIQUE_ADDRESS Context ctx_;
  };
};

template <typename Receiver, typename Fn, typename Context>
using _operation = typename _op<Receiver, Fn, Context>::type;

template <typename Fn, typename Context>
struct _snd_base {
  struct type {
    template <template<typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template (typename Self, typename Receiver)
      (requires derived_from<remove_cvref_t<Self>, type> AND
        constructible_from<Fn, member_t<Self, Fn>> AND
        constructible_from<Context, member_t<Self, Context>>)
    friend _operation<remove_cvref_t<Receiver>, Fn, Context>
    tag_invoke(tag_t<connect>, Self&& self, Receiver&& rec)
        noexcept(std::is_nothrow_constructible_v<
          _operation<Receiver, Fn, Context>,
          Receiver,
          member_t<Self, Fn>,
          member_t<Self, Context>>) {
      return _operation<remove_cvref_t<Receiver>, Fn, Context>{
        (Receiver&&) rec,
        ((Self&&) self).fn_,
        ((Self&&) self).ctx_};
    }

    UNIFEX_NO_UNIQUE_ADDRESS Fn fn_;
    UNIFEX_NO_UNIQUE_ADDRESS Context ctx_{};
  };
};

template <typename Fn, typename Context, typename... ValueTypes>
struct _snd {
  struct type : _snd_base<Fn, Context>::type {
    template <template<typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<ValueTypes...>>;
  };
};

template <typename Fn, typename... ValueTypes>
using _sender = typename _snd<Fn, detail::_empty<>, ValueTypes...>::type;

template <typename Fn, typename Context, typename... ValueTypes>
using _sender_with_context = typename _snd<Fn, Context, ValueTypes...>::type;

template <typename T>
T void_cast(void* pv) noexcept {
  return static_cast<T&&>(*static_cast<std::add_pointer_t<T>>(pv));
}

namespace _cpo {
template <typename... ValueTypes>
struct _fn {
  template (typename Fn)
    (requires move_constructible<Fn>)
  _sender<Fn, ValueTypes...> operator()(Fn fn) const
      noexcept(std::is_nothrow_constructible_v<_sender<Fn, ValueTypes...>, Fn>) {
    return _sender<Fn, ValueTypes...>{{(Fn&&) fn}};
  }
  template (typename Fn, typename Context)
    (requires move_constructible<Fn> AND move_constructible<Context>)
  _sender_with_context<Fn, Context, ValueTypes...> operator()(Fn fn, Context ctx) const
      noexcept(std::is_nothrow_constructible_v<
          _sender_with_context<Fn, Context, ValueTypes...>,
          Fn,
          Context>) {
    return _sender_with_context<Fn, Context, ValueTypes...>{{(Fn&&) fn, (Context&&) ctx}};
  }
};
} // namespace _cpo
} // namespace _create

/**
 * \fn template <class... ValueTypes> auto create(auto fn [, auto ctx])
 * \brief A utility for building a sender-based API out of a C-style API that
 *        accepts a void* context and a function pointer continuation.
 * 
 * \em Example:
 * \code
 *  // A void-returning C-style async API that accepts a context and a continuation:
 *  using callback_t = void(void* context, int result);
 *  void old_c_style_api(int a, int b, void* context, callback_t* callback_fn);
 *
 *  // A sender-based async API implemented in terms of the C-style API (using C++20):
 *  unifex::typed_sender auto new_sender_api(int a, int b) {
 *    return unifex::create<int>([=](auto& rec) {
 *      old_c_style_api(a, b, &rec, [](void* context, int result) {
 *        unifex::void_cast<decltype(rec)>(context).set_value(result);
 *      });
 *    });
 *  }
 * \endcode
 * \tparam ValueTypes The value types of the resulting sender. Should be the list of
 *   value type(s) accepted by the callback (with the exception of the void*
 *   context).
 * \param[in] fn A void-returning callable that accepts an lvalue reference to an object
 *   whose type satisfies the \c unifex::receiver_of<ValueTypes...> concept. This function
 *   should dispatch to the C-style callback (see example).
 * \param[in] ctx An optional extra bit of data to be bundled with the receiver passed to
 *   \c fn. E.g., if \c fn is a lambda that accepts <tt>(auto& rec)</tt> and \c ctx is 42,
 *   then from within the body of \c fn , the value of the expression \c rec.context() is
 *   42.
 * \return A sender that, when connected and started, dispatches to the wrapped C-style
 *   API with the callback of your choosing. The receiver passed to \c fn wraps the
 *   receiver passed to \c connect . Your callback should "complete" the receiver passed
 *   to \c fn , which will complete the receiver passed to \c connect in turn.
 */
template <typename... ValueTypes>
inline constexpr _create::_cpo::_fn<ValueTypes...> create {};

using _create::void_cast;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
