#pragma once

#include <unifex/config.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>

#include <functional>
#include <type_traits>

namespace unifex {

template <typename Predecessor, typename Func>
struct transform_sender {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

  template <template <typename...> class Tuple>
  struct transform_result {
   private:
    template <typename Result, typename = void>
    struct impl {
      using type = Tuple<Result>;
    };
    template <typename Result>
    struct impl<Result, std::enable_if_t<std::is_void_v<Result>>> {
      using type = Tuple<>;
    };

   public:
    template <typename... Args>
    using apply = typename impl<std::invoke_result_t<Func, Args...>>::type;
  };

  template <typename... Args>
  using is_overload_noexcept = std::bool_constant<noexcept(
      std::invoke(std::declval<Func>(), std::declval<Args>()...))>;

  template <template <typename...> class Variant>
  struct calculate_errors {
   public:
    template <typename... Errors>
    using apply = std::conditional_t<
        Predecessor::
            template value_types<std::conjunction, is_overload_noexcept>::value,
        Variant<Errors...>,
        deduplicate_t<Variant<Errors..., std::exception_ptr>>>;
  };

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = deduplicate_t<typename Predecessor::template value_types<
      Variant,
      transform_result<Tuple>::template apply>>;

  template <template <typename...> class Variant>
  using error_types = typename Predecessor::template error_types<
      calculate_errors<Variant>::template apply>;

  friend constexpr auto tag_invoke(
      tag_t<cpo::blocking>,
      const transform_sender& sender) {
    return cpo::blocking(sender.pred_);
  }

  template <typename Receiver>
  struct transform_receiver {
    UNIFEX_NO_UNIQUE_ADDRESS Func func_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    template <typename... Values>
    void value(Values&&... values) && noexcept {
      using result_type = std::invoke_result_t<Func, Values...>;
      if constexpr (std::is_void_v<result_type>) {
        if constexpr (noexcept(std::invoke(
                          (Func &&) func_, (Values &&) values...))) {
          std::invoke((Func &&) func_, (Values &&) values...);
          cpo::set_value((Receiver &&) receiver_);
        } else {
          try {
            std::invoke((Func &&) func_, (Values &&) values...);
            cpo::set_value((Receiver &&) receiver_);
          } catch (...) {
            cpo::set_error((Receiver &&) receiver_, std::current_exception());
          }
        }
      } else {
        if constexpr (noexcept(std::invoke(
                          (Func &&) func_, (Values &&) values...))) {
          cpo::set_value(
              (Receiver &&) receiver_,
              std::invoke((Func &&) func_, (Values &&) values...));
        } else {
          try {
            cpo::set_value(
                (Receiver &&) receiver_,
                std::invoke((Func &&) func_, (Values &&) values...));
          } catch (...) {
            cpo::set_error((Receiver &&) receiver_, std::current_exception());
          }
        }
      }
    }

    template <typename Error>
    void error(Error&& error) && noexcept {
      cpo::set_error((Receiver &&) receiver_, (Error &&) error);
    }

    void done() && noexcept {
      cpo::set_done((Receiver &&) receiver_);
    }

    template <
        typename CPO,
        std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
    friend auto tag_invoke(CPO cpo, const transform_receiver& r) noexcept(
        std::is_nothrow_invocable_v<CPO, const Receiver&>)
        -> std::invoke_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.receiver_));
    }

    template <typename Visit>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const transform_receiver& r,
        Visit&& visit) {
      std::invoke(visit, r.receiver_);
    }
  };

  template <typename Receiver>
  auto connect(Receiver&& receiver) && {
    return cpo::connect(
        std::forward<Predecessor>(pred_),
        transform_receiver<std::remove_cvref_t<Receiver>>{
            std::forward<Func>(func_), std::forward<Receiver>(receiver)});
  }
};

template <typename Sender, typename Func>
auto transform(Sender&& predecessor, Func&& func) {
  return transform_sender<std::remove_cvref_t<Sender>, std::decay_t<Func>>{
      (Sender &&) predecessor, (Func &&) func};
}

} // namespace unifex
