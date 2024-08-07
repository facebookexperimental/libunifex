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

#include <unifex/bind_back.hpp>
#include <unifex/get_allocator.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _with_query_value {

template <typename CPO, typename Value, typename Receiver>
struct _receiver_wrapper {
  class type;
};
template <typename CPO, typename Value, typename Receiver>
using receiver_wrapper = typename _receiver_wrapper<CPO, Value, Receiver>::type;

template <typename CPO, typename Value, typename Receiver>
class _receiver_wrapper<CPO, Value, Receiver>::type {
public:
  template <typename Receiver2>
  explicit type(Receiver2&& receiver, const Value& val)
    : receiver_((Receiver2 &&) receiver)
    , val_(&val) {}

  template <typename... T>
  void set_value(T&&... ts) noexcept {
    UNIFEX_TRY {
      unifex::set_value(std::move(receiver_), std::forward<T>(ts)...);
    }
    UNIFEX_CATCH(...) {
      unifex::set_error(std::move(receiver_), std::current_exception());
    }
  }

  template <typename E>
  void set_error(E&& e) noexcept {
    unifex::set_error(std::move(receiver_), std::forward<E>(e));
  }

  void set_done() noexcept {
    unifex::set_done(std::move(receiver_));
  }

private:
  friend const Value& tag_invoke(CPO, const type& r) noexcept {
    return *r.val_;
  }

  template(typename OtherCPO, typename R)                                //
      (requires is_receiver_query_cpo_v<OtherCPO> AND same_as<R, type>)  //
      friend auto tag_invoke(OtherCPO cpo, const R& r) noexcept(
          std::is_nothrow_invocable_v<OtherCPO, const Receiver&>)
          -> std::invoke_result_t<OtherCPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Visit>
  friend void
  tag_invoke(tag_t<visit_continuations>, const type& r, Visit&& visit) {
    std::invoke(visit, r.receiver_);
  }
#endif

  Receiver receiver_;
  const Value* val_;
};

template <typename CPO, typename Value, typename Sender, typename Receiver>
struct _op {
  class type;
};
template <typename CPO, typename Value, typename Sender, typename Receiver>
using operation =
    typename _op<CPO, Value, Sender, remove_cvref_t<Receiver>>::type;

template <typename CPO, typename Value, typename Sender, typename Receiver>
class _op<CPO, Value, Sender, Receiver>::type {
public:
  template <typename Receiver2, typename Value2>
  explicit type(Sender&& sender, Receiver2&& receiver, Value2&& value)
    : value_((Value2 &&) value)
    , innerOp_(connect(
          (Sender &&) sender,
          receiver_wrapper<CPO, Value, Receiver>{
              (Receiver2 &&) receiver, value_})) {}

  void start() & noexcept { unifex::start(innerOp_); }

private:
  UNIFEX_NO_UNIQUE_ADDRESS Value value_;
  /*UNIFEX_NO_UNIQUE_ADDRESS*/
  connect_result_t<Sender, receiver_wrapper<CPO, Value, Receiver>> innerOp_;
};

template <typename CPO, typename Value, typename Sender>
struct _sender {
  class type;
};
template <typename CPO, typename Value, typename Sender>
using sender =
    typename _sender<CPO, std::decay_t<Value>, remove_cvref_t<Sender>>::type;

template <typename CPO, typename Value, typename Sender>
class _sender<CPO, Value, Sender>::type {
public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = sender_value_types_t<Sender, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<Sender, Variant>;

  static constexpr bool sends_done = sender_traits<Sender>::sends_done;

  static constexpr blocking_kind blocking = sender_traits<Sender>::blocking;

  static constexpr bool is_always_scheduler_affine =
      sender_traits<Sender>::is_always_scheduler_affine;

  template <typename Sender2, typename Value2>
  explicit type(Sender2&& sender, Value2&& value)
    : sender_((Sender2 &&) sender)
    , value_((Value2 &&) value) {}

  template(typename Self, typename Receiver)  //
      (requires same_as<remove_cvref_t<Self>, type> AND
           constructible_from<Value, member_t<Self, Value>> AND receiver<Receiver> AND
           sender_to<
               member_t<Self, Sender>,
               receiver_wrapper<
                   CPO,
                   Value,
                   remove_cvref_t<Receiver>>>)  //
      friend auto tag_invoke(
          tag_t<unifex::connect>,
          Self&& s,
          Receiver&&
              receiver) noexcept(std::
                                     is_nothrow_constructible_v<
                                         Value,
                                         member_t<Self, Value>>&&
                                         is_nothrow_connectable_v<
                                             member_t<Self, Sender>,
                                             receiver_wrapper<
                                                 CPO,
                                                 Value,
                                                 remove_cvref_t<Receiver>>>)
          -> operation<CPO, Value, member_t<Self, Sender>, Receiver> {
    return operation<CPO, Value, member_t<Self, Sender>, Receiver>{
        static_cast<Self&&>(s).sender_,
        static_cast<Receiver&&>(receiver),
        static_cast<Self&&>(s).value_};
  }

  friend constexpr blocking_kind
  tag_invoke(tag_t<unifex::blocking>, const type& s) noexcept {
    return unifex::blocking(s.sender_);
  }

private:
  Sender sender_;
  Value value_;
};
}  // namespace _with_query_value

namespace _with_query_value_cpo {
inline const struct _fn {
  template <typename Sender, typename CPO, typename Value>
  _with_query_value::sender<CPO, Value, Sender>
  operator()(Sender&& sender, CPO, Value&& value) const {
    static_assert(
        std::is_empty_v<CPO>,
        "with_query_value() does not support stateful CPOs");
    return _with_query_value::sender<CPO, Value, Sender>{
        (Sender &&) sender, (Value &&) value};
  }
  template <typename CPO, typename Value>
  constexpr auto operator()(const CPO&, Value&& value) const
      noexcept(std::is_nothrow_invocable_v<tag_t<bind_back>, _fn, CPO, Value>)
          -> bind_back_result_t<_fn, CPO, Value> {
    return bind_back(*this, CPO{}, (Value &&) value);
  }
} with_query_value{};
}  // namespace _with_query_value_cpo
using _with_query_value_cpo::with_query_value;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
