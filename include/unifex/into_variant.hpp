/*
 * Copyright (c) Rishabh Dwivedi <rishabhdwivedi17@gmail.com>
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
#include <unifex/bind_back.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_list.hpp>

#include <tuple>
#include <variant>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _into_variant {
template <typename Receiver, typename VariantType>
struct _receiver {
  struct type;
};
template <typename Receiver, typename VariantType>
using receiver_t = typename _receiver<Receiver, VariantType>::type;

template <typename Receiver, typename VariantType>
struct _receiver<Receiver, VariantType>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  template(typename... Values)
      (requires receiver_of<Receiver, Values...>)
  void set_value(Values&&... values) && {
    unifex::set_value((Receiver &&) receiver_,
        VariantType(std::make_tuple((Values &&)(values)...)));
  }

  template(typename Error)
      (requires receiver<Receiver, Error>)
  void set_error(Error&& error) && noexcept {
    unifex::set_error((Receiver &&)(receiver_), (Error &&)(error));
  }

  void set_done() && noexcept {
    unifex::set_done((Receiver &&) receiver_);
  }

  template(typename CPO)
      (requires is_receiver_query_cpo_v<CPO> AND is_callable_v<CPO, const Receiver&>)
  friend auto tag_invoke(CPO cpo, const type& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

  template <typename Visit>
  friend void tag_invoke(tag_t<visit_continuations>, const type& self, Visit&& visit) {
    std::invoke(visit, self.receiver_);
  }
};

template <typename Predecessor>
struct _sender {
  struct type;
};
template <typename Predecessor>
using sender = typename _sender<remove_cvref_t<Predecessor>>::type;

template <typename Predecessor>
struct _sender<Predecessor>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<
      sender_value_types_t<Predecessor, std::variant, std::tuple>>>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<Predecessor, Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  template <typename Receiver>
  using receiver_t = receiver_t<Receiver, sender_value_types_t<Predecessor, std::variant, std::tuple>>;

  friend constexpr auto tag_invoke(tag_t<blocking>, const type& sender) {
    return blocking(sender.pred_);
  }

  template(typename Sender, typename Receiver)
    (requires same_as<remove_cvref_t<Sender>, type> AND receiver<Receiver> AND
        sender_to<member_t<Sender, Predecessor>, receiver_t<remove_cvref_t< Receiver>>>)
  friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r) 
    noexcept(
      std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver> &&
      is_nothrow_connectable_v<member_t< Sender, Predecessor>, receiver_t<remove_cvref_t<Receiver>>>)
      -> connect_result_t<member_t<Sender, Predecessor>, receiver_t<remove_cvref_t<Receiver>>> {
    return unifex::connect(
      static_cast<Sender&&>(s).pred_,
      receiver_t<remove_cvref_t<Receiver>>{
        static_cast<Receiver&&>(r)});
  }
};

namespace _cpo {
  struct _fn {
  private:
    template <typename Sender>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, Sender>,
        meta_tag_invoke_result<_fn>,
        meta_quote1<_into_variant::sender>>::template apply<Sender>;
  
  public:
    template(typename Sender)
      (requires tag_invocable<_fn, Sender>)
    auto operator()(Sender&& predecessor) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Sender>)
        -> _result_t<Sender> {
      return unifex::tag_invoke(_fn{}, (Sender &&)(predecessor));
    }
  
    template(typename Sender)
      (requires(!tag_invocable<_fn, Sender>))
    auto operator()(Sender&& predecessor) const 
        noexcept(std::is_nothrow_constructible_v<
          _into_variant::sender<Sender>, Sender>)
        -> _result_t<Sender> {
      return _into_variant::sender<Sender>{(Sender &&) predecessor};
    }
    constexpr auto operator()() const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn>)
        -> bind_back_result_t<_fn> {
      return bind_back(*this);
    }
  };
}  // namespace _cpo
}  // namespace _into_variant

inline constexpr _into_variant::_cpo::_fn into_variant{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
