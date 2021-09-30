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
#include <unifex/config.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/bind_back.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>

#include <functional>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _bulk {
template <typename Receiver, typename Shape, typename Func>
struct _receiver {
  struct type;
};

template <typename Receiver, typename Shape, typename Func>
using receiver_t = typename _receiver<Receiver, Shape, Func>::type;

template <typename Receiver, typename Shape, typename Func>
struct _receiver<Receiver, Shape, Func>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  UNIFEX_NO_UNIQUE_ADDRESS Shape shape_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

  template(typename... Values)
      (requires receiver_of< Receiver, Values...>)
  void set_value(Values&&... values) && {
    UNIFEX_TRY {
      for (Shape i{0}; i < shape_; i++) {
        func_(i, values...);
      }
      unifex::set_value((Receiver &&) receiver_, ((Values &&) values)...);
    }
    UNIFEX_CATCH(...) {
      unifex::set_error((Receiver &&) receiver_, std::current_exception());
    }
  }

  template(typename Error)
      (requires receiver<Receiver, Error>)
  void set_error(Error&& error) && noexcept {
    unifex::set_error((Receiver &&)(receiver_), (Error &&)(error));
  }

  void set_done() && noexcept {
    unifex::set_done((Receiver &&)(receiver_));
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

template <typename Predecessor, typename Shape, typename Func>
struct _sender {
  struct type;
};

template <typename Predecessor, typename Shape, typename Func>
using sender = typename _sender<
    remove_cvref_t<Predecessor>,
    remove_cvref_t<Shape>,
    std::decay_t<Func>>::type;

template <typename Predecessor, typename Shape, typename Func>
struct _sender<Predecessor, Shape, Func>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Shape shape_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = sender_value_types_t<Predecessor, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Predecessor, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  template <typename Receiver>
  using receiver_t = receiver_t<Receiver, Shape, Func>;

  friend constexpr auto tag_invoke(tag_t<blocking>, const type& sender) {
    return blocking(sender.pred_);
  }

  template(typename Sender, typename Receiver)
    (requires same_as<remove_cvref_t<Sender>, type> AND receiver<Receiver> AND
        sender_to<member_t<Sender, Predecessor>, receiver_t<remove_cvref_t<Receiver>>>)
  friend auto tag_invoke( tag_t<unifex::connect>, Sender&& s, Receiver&& r) 
    noexcept(
      std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver> &&
      std::is_nothrow_constructible_v<Func, member_t<Sender, Func>> &&
      std::is_nothrow_constructible_v<Shape, member_t<Sender, Shape>> &&
      is_nothrow_connectable_v< member_t<Sender, Predecessor>, receiver_t<remove_cvref_t<Receiver>>>)
      -> connect_result_t<member_t<Sender, Predecessor>, receiver_t<remove_cvref_t<Receiver>>> {
    return unifex::connect(
        ((Sender &&) s).pred_,
        receiver_t<remove_cvref_t<Receiver>>{
          (Receiver &&)(r), ((Sender &&) s).shape_, ((Sender &&) s).func_});
  }
};

namespace _cpo {
  struct _fn {
  private:
    template <typename Sender, typename Shape, typename Func>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, Sender, Shape, Func>,
        meta_tag_invoke_result<_fn>,
        meta_quote3<_bulk::sender>>::template apply<Sender, Shape, Func>;
  
  public:
    template(typename Sender, typename Shape, typename Func)
      (requires tag_invocable<_fn, Sender, Shape, Func> AND
         std::is_integral_v<remove_cvref_t<Shape>>)
    auto operator()(Sender&& predecessor, Shape&& shape, Func&& func) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Shape, Func>)
        -> _result_t<Sender, Shape, Func> {
      return unifex::tag_invoke(_fn{}, (Sender &&)(predecessor), (Shape &&)(shape), (Func &&)(func));
    }
  
    template(typename Sender, typename Shape, typename Func)
      (requires(!tag_invocable<_fn, Sender, Shape, Func>) AND
         std::is_integral_v<remove_cvref_t<Shape>>)
    auto operator()(Sender&& predecessor, Shape&& shape, Func&& func) const
        noexcept(std::is_nothrow_constructible_v<_bulk::sender<Sender, Shape, Func>, Sender, Shape, Func>)
        -> _result_t<Sender, Shape, Func> {
      return _bulk::sender<Sender, Shape, Func>{
          (Sender &&)(predecessor), (Shape &&)(shape), (Func &&)(func)};
    }

    template(typename Shape, typename Func)
      (requires std::is_integral_v<remove_cvref_t<Shape>>)
    constexpr auto operator()(Shape&& shape, Func&& func) const
        noexcept(is_nothrow_callable_v<tag_t<bind_back>, _fn, Shape, Func>)
        -> bind_back_result_t<_fn, Shape, Func> {
      return bind_back(*this, (Shape &&)(shape), (Func &&)(func));
    }
  };
}  // namespace _cpo
}  // namespace _bulk

inline constexpr _bulk::_cpo::_fn bulk{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
