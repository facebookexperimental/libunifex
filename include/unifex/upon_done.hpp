#pragma once

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
namespace _upon_done {

template <typename Receiver, std::invocable Func>
struct _receiver {
  struct type;
};
template <typename Receiver, std::invocable Func>
using receiver_t = typename _receiver<Receiver, Func>::type;

template <typename Receiver, std::invocable Func>
struct _receiver<Receiver, Func>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  template <typename... Values>
  void set_value(Values&&... values) && noexcept {
    unifex::set_value(std::move(receiver_), std::forward<Values>(values)...);
  }

  template <typename Error>
  void set_error(Error&& error) && noexcept {
    unifex::set_error(std::move(receiver_), std::forward<Error>(error));
  }

  void set_done() && noexcept {
    if constexpr (noexcept(std::invoke(std::move(func_)))) {
      std::invoke(std::move(func_));
      unifex::set_done(std::move(receiver_));
    } else {
      UNIFEX_TRY {
        std::invoke(std::move(func_));
        unifex::set_value(std::move(receiver_));
      }
      UNIFEX_CATCH(...) {
        unifex::set_error(std::move(receiver_), std::current_exception());
      }
    }
  }

  template <typename CPO, typename R>
  requires is_receiver_query_cpo_v<CPO> AND same_as<R, type>
  friend auto tag_invoke(CPO cpo, const R& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

  template <typename Visit>
  friend void
  tag_invoke(tag_t<visit_continuations>, const type& self, Visit&& visit) {
    std::invoke(visit, self.receiver_);
  }
};

template <typename Predecessor, typename Func>
struct _sender {
  struct type;
};
template <typename Predecessor, typename Func>
using sender =
    typename _sender<remove_cvref_t<Predecessor>, std::decay_t<Func>>::type;

template <typename Predecessor, typename Func>
struct _sender<Predecessor, Func>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = sender_value_types_t<Predecessor, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Predecessor, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  template <typename Receiver>
  using receiver_t = receiver_t<Receiver, Func>;

  friend constexpr auto tag_invoke(tag_t<blocking>, const type& sender) {
    return blocking(sender.pred_);
  }

  template <typename Sender, typename Receiver>
  requires same_as<remove_cvref_t<Sender>, type> AND receiver<Receiver> AND
      sender_to<
          member_t<Sender, Predecessor>,
          receiver_t<remove_cvref_t<Receiver>>>
  friend auto
  tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r) noexcept(
      std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver>&&
          std::is_nothrow_constructible_v<Func, member_t<Sender, Func>>&&
              is_nothrow_connectable_v<
                  member_t<Sender, Predecessor>,
                  receiver_t<remove_cvref_t<Receiver>>>)
      -> connect_result_t<
          member_t<Sender, Predecessor>,
          receiver_t<remove_cvref_t<Receiver>>> {
    return unifex::connect(
        static_cast<Sender&&>(s).pred_,
        receiver_t<remove_cvref_t<Receiver>>{
            static_cast<Sender&&>(s).func_, static_cast<Receiver&&>(r)});
  }
};

namespace _cpo {
struct _fn {
private:
  template <typename Sender, typename Func>
  using _result_t = typename conditional_t<
      tag_invocable<_fn, Sender, Func>,
      meta_tag_invoke_result<_fn>,
      meta_quote2<_upon_done::sender>>::template apply<Sender, Func>;

public:
  template <typename Sender, typename Func>
  requires tag_invocable<_fn, Sender, Func>
  auto operator()(Sender&& predecessor, Func&& func) const
      noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Func>)
          -> _result_t<Sender, Func> {
    return unifex::tag_invoke(
        _fn{}, std::forward<Sender>(predecessor), std::forward<Func>(func));
  }

  template <typename Sender, typename Func>
  requires(!tag_invocable<_fn, Sender, Func>) auto
  operator()(Sender&& predecessor, Func&& func) const
      noexcept(std::is_nothrow_constructible_v<
               _upon_done::sender<Sender, Func>,
               Sender,
               Func>) -> _result_t<Sender, Func> {
    return _upon_done::sender<Sender, Func>{
        std::forward<Sender>(predecessor), std::forward<Func>(func)};
  }
  template <typename Func>
  constexpr auto operator()(Func&& func) const
      noexcept(is_nothrow_callable_v<tag_t<bind_back>, _fn, Func>)
          -> bind_back_result_t<_fn, Func> {
    return bind_back(*this, std::forward<Func>(func));
  }
};
};  // namespace _cpo
}  // namespace _upon_done
inline constexpr _upon_done::_cpo::_fn upon_done{};
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
