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
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/execution_policy.hpp>
#include <unifex/type_list.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _let_v_w {

template<typename StateFactory, typename InnerOp, typename Receiver>
struct _operation {
  struct type;
};

template <typename StateFactory, typename InnerOp, typename Receiver>
using operation =  typename _operation<
  StateFactory, InnerOp, Receiver>::type;

template<typename StateFactory, typename SuccessorFactory>
struct _sender {
  class type;
};

template<typename StateFactory, typename SuccessorFactory>
using let_with_sender = typename _sender<StateFactory, SuccessorFactory>::type;

template<typename StateFactory, typename SuccessorFactory>
class _sender<StateFactory, SuccessorFactory>::type {
public:
  using InnerOp = std::invoke_result_t<SuccessorFactory, callable_result_t<StateFactory>&>;

  template<template<typename...> class Variant, template<typename...> class Tuple>
  using value_types = sender_value_types_t<InnerOp, Variant, Tuple>;

  template<template<typename...> class Variant>
  using error_types = sender_error_types_t<InnerOp, Variant>;

  static constexpr bool sends_done = sender_traits<InnerOp>::sends_done;

  template<typename StateFactory2, typename SuccessorFactory2>
  explicit type(StateFactory2&& stateFactory, SuccessorFactory2&& func) :
    stateFactory_((StateFactory2&&)stateFactory),
    func_((SuccessorFactory2&&)func)
  {}

  template(typename Self, typename Receiver)
    (requires same_as<remove_cvref_t<Self>, type> AND receiver<Receiver>)
  friend auto tag_invoke(tag_t<unifex::connect>, Self&& self, Receiver&& r)
    noexcept(
      is_nothrow_callable_v<member_t<Self, StateFactory>> &&
      std::is_nothrow_invocable_v<
        member_t<Self, SuccessorFactory>,
        callable_result_t<member_t<Self, StateFactory>>&> &&
      is_nothrow_connectable_v<
        callable_result_t<
          member_t<Self, SuccessorFactory>,
          callable_result_t<member_t<Self, StateFactory>>&>,
        remove_cvref_t<Receiver>>) {

    return operation<StateFactory, SuccessorFactory, Receiver>(
      static_cast<Self&&>(self).stateFactory_,
      static_cast<Self&&>(self).func_,
      static_cast<Receiver&&>(r));
  }

private:
  UNIFEX_NO_UNIQUE_ADDRESS StateFactory stateFactory_;
  UNIFEX_NO_UNIQUE_ADDRESS SuccessorFactory func_;
};


template<typename StateFactory, typename SuccessorFactory, typename Receiver>
struct _operation<StateFactory, SuccessorFactory, Receiver>::type {
  type(StateFactory&& stateFactory, SuccessorFactory&& func, Receiver&& r) :
    stateFactory_(static_cast<StateFactory&&>(stateFactory)),
    func_(static_cast<SuccessorFactory&&>(func)),
    state_(static_cast<StateFactory&&>(stateFactory_)()),
    innerOp_(
        unifex::connect(
        static_cast<SuccessorFactory&&>(func_)(state_),
        static_cast<Receiver&&>(r))) {
  }

  void start() & noexcept {
    unifex::start(innerOp_);
  }

  StateFactory stateFactory_;
  SuccessorFactory func_;
  callable_result_t<StateFactory> state_;
  connect_result_t<
    callable_result_t<SuccessorFactory, callable_result_t<StateFactory>&>,
    remove_cvref_t<Receiver>>
    innerOp_;
};

namespace _cpo {
  struct _fn {
    template (typename StateFactory, typename SuccessorFactory)
      (requires
        callable<std::decay_t<StateFactory>> AND
        callable<
          std::decay_t<SuccessorFactory>,
          callable_result_t<std::decay_t<StateFactory>>&> AND
        sender<
          callable_result_t<
            std::decay_t<SuccessorFactory>,
            callable_result_t<std::decay_t<StateFactory>>&>>)
    auto operator()(StateFactory&& stateFactory, SuccessorFactory&& successor_factory) const
      noexcept(std::is_nothrow_constructible_v<std::decay_t<SuccessorFactory>, SuccessorFactory> &&
          std::is_nothrow_constructible_v<std::decay_t<StateFactory>, StateFactory>)
      -> _let_v_w::let_with_sender<
         std::decay_t<StateFactory>, std::decay_t<SuccessorFactory>> {
      return _let_v_w::let_with_sender<
        std::decay_t<StateFactory>, std::decay_t<SuccessorFactory>>{
        (StateFactory&&)stateFactory, (SuccessorFactory&&)successor_factory};
    }
  };
} // namespace _cpo
} // namespace _let_v_w

inline constexpr _let_v_w::_cpo::_fn let_value_with{};

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
