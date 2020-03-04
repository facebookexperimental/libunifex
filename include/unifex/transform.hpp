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
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/type_list.hpp>

#include <functional>
#include <type_traits>
#include <utility>

namespace unifex {

namespace detail
{
  template<typename Func>
  struct transform_result_overload {
  private:
    template<typename Result>
    struct impl {
      using type = type_list<Result>;
    };

    template<>
    struct impl<void> {
      using type = type_list<>;
    };
  public:
    template<typename... Args>
    using apply = type_list<typename impl<std::invoke_result_t<Func, Args...>>::type>;
  };

  template<typename Predecessor, typename Func, typename Receiver>
  struct transform_operation {
    class type;
  };

  template<typename Predecessor, typename Func, typename Receiver>
  struct transform_receiver {
    class type {
      using operation_state = typename transform_operation<Predecessor, Func, Receiver>::type;
    public:
      explicit type(operation_state* op) noexcept
      : op_(op)
      {}

      type(type&& other) noexcept
      : op_(std::exchange(other.op_, nullptr))
      {}

      template <typename... Values>
      void set_value(Values&&... values) && noexcept {
        using result_type = std::invoke_result_t<Func, Values...>;
        if constexpr (std::is_void_v<result_type>) {
          if constexpr (std::is_nothrow_invocable_v<Func, Values...> &&
                        std::is_nothrow_invocable_v<decltype(unifex::set_value), Receiver>) {
            std::invoke((Func &&) op_->func_, (Values &&) values...);
            unifex::set_value((Receiver &&) op_->receiver_);
          } else {
            try {
              std::invoke((Func &&) op_->func_, (Values &&) values...);
              unifex::set_value((Receiver &&) op_->receiver_);
            } catch (...) {
              unifex::set_error((Receiver &&) op_->receiver_, std::current_exception());
            }
          }
        } else {
          if constexpr (std::is_nothrow_invocable_v<Func, Values...> &&
                        std::is_nothrow_invocable_v<decltype(unifex::set_value), result_type>) {
            unifex::set_value(
              (Receiver &&) op_->receiver_,
              std::invoke((Func &&) op_->func_, (Values &&) values...));
          } else {
            try {
              unifex::set_value(
                (Receiver &&) op_->receiver_,
                std::invoke((Func &&) op_->func_, (Values &&) values...));
            } catch (...) {
              unifex::set_error((Receiver &&) op_->receiver_, std::current_exception());
            }
          }
        }
      }

      template <typename Error>
      void set_error(Error&& error) && noexcept {
        unifex::set_error((Receiver &&) op_->receiver_, (Error &&) error);
      }

      void set_done() && noexcept {
        unifex::set_done((Receiver &&) op_->receiver_);
      }

    private:
      template <
        typename CPO,
        std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(CPO cpo, const type& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
        return std::move(cpo)(r.get_const_receiver());
      }

      template <typename Visit>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const type& r,
          Visit&& visit) {
        std::invoke(visit, r.get_const_receiver());
      }

      const Receiver& get_const_receiver() const noexcept {
        return op_->receiver_;
      }

      operation_state* op_;
    };
  };

  template<typename Predecessor, typename Func, typename Receiver>
  class transform_operation<Predecessor, Func, Receiver>::type {
    using receiver_wrapper = typename transform_receiver<Predecessor, Func, Receiver>::type;

  public:
    template<typename Func2, typename Receiver2>
    explicit type(Predecessor&& pred, Func2&& func, Receiver2&& receiver)
    : func_((Func2&&)func)
    , receiver_((Receiver2&&)receiver)
    , op_(unifex::connect((Predecessor&&)pred, receiver_wrapper{this}))
    {}

    void start() & noexcept {
      unifex::start(op_);
    }

  private:
    friend class transform_receiver<Predecessor, Func, Receiver>::type;

    UNIFEX_NO_UNIQUE_ADDRESS Func func_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    operation_t<Predecessor, typename transform_receiver<Predecessor, Func, Receiver>::type> op_;
  };

  template<typename Predecessor, typename Func>
  struct transform_sender {
    class type {
    public:
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types = type_list_nested_apply_t<
        typename Predecessor::template value_types<
          concat_type_lists_unique_t,
          transform_result_overload<Func>::template apply>,
        Variant,
        Tuple>;

      template <template <typename...> class Variant>
      using error_types = typename concat_type_lists_unique_t<
        typename Predecessor::template error_types<type_list>,
        type_list<std::exception_ptr>>::template apply<Variant>;

      template<typename Predecessor2, typename Func2>
      explicit type(Predecessor2&& pred, Func2&& func)
      : pred_((Predecessor2&&)pred)
      , func_((Func2&&)func)
      {}

    private:

      template <
        typename Receiver,
        std::enable_if_t<
          is_connectable_v<
            Predecessor,
            typename transform_receiver<Predecessor, Func, std::decay_t<Receiver>>::type>, int> = 0>
      friend auto tag_invoke(tag_t<connect>, type&& s, Receiver&& receiver)
          noexcept(std::is_nothrow_move_constructible_v<Func> &&
                   std::is_nothrow_constructible_v<std::decay_t<Receiver>, Receiver> &&
                   is_nothrow_connectable_v<Predecessor, typename transform_receiver<Predecessor, Func, std::decay_t<Receiver>>::type>)
          -> typename transform_operation<Predecessor, Func, std::decay_t<Receiver>>::type {
        return typename transform_operation<Predecessor, Func, std::decay_t<Receiver>>::type{
          (Predecessor&&)s.pred_,
          (Func&&)s.func_,
          (Receiver&&)receiver
        };
      }


      template <
        typename Receiver,
        std::enable_if_t<
          is_connectable_v<
            Predecessor&,
            typename transform_receiver<Predecessor&, Func, std::decay_t<Receiver>>::type>, int> = 0>
      friend auto tag_invoke(tag_t<connect>, type& s, Receiver&& receiver)
          noexcept(std::is_nothrow_constructible_v<Func, Func&> &&
                   std::is_nothrow_constructible_v<std::decay_t<Receiver>, Receiver> &&
                   is_nothrow_connectable_v<Predecessor&, typename transform_receiver<Predecessor&, Func, std::decay_t<Receiver>>::type>)
          -> typename transform_operation<Predecessor&, Func, std::decay_t<Receiver>>::type {
        return typename transform_operation<Predecessor&, Func, std::decay_t<Receiver>>::type{
          s.pred_,
          s.func_,
          (Receiver&&)receiver
        };
      }

      template <
        typename Receiver,
        std::enable_if_t<
          is_connectable_v<
            const Predecessor&,
            typename transform_receiver<const Predecessor&, Func, std::decay_t<Receiver>>::type>, int> = 0>
      friend auto tag_invoke(tag_t<connect>, const type& s, Receiver&& receiver)
          noexcept(std::is_nothrow_copy_constructible_v<Func> &&
                   std::is_nothrow_constructible_v<std::decay_t<Receiver>, Receiver> &&
                   is_nothrow_connectable_v<const Predecessor&, typename transform_receiver<const Predecessor&, Func, std::decay_t<Receiver>>::type>)
          -> typename transform_operation<const Predecessor&, Func, std::decay_t<Receiver>>::type {
        return typename transform_operation<const Predecessor&, Func, std::decay_t<Receiver>>::type{
          std::as_const(s.pred_),
          std::as_const(s.func_),
          (Receiver&&)receiver
        };
      }

      friend constexpr auto tag_invoke(
          tag_t<blocking>,
          const type& sender) {
        return blocking(sender.pred_);
      }

      UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
      UNIFEX_NO_UNIQUE_ADDRESS Func func_;
    };
  };
} // namespace detail

inline constexpr struct transform_cpo {
  template<
    typename Sender,
    typename Func,
    std::enable_if_t<is_tag_invocable_v<transform_cpo, Sender, Func>, int> = 0>
  auto operator()(Sender&& sender, Func&& func) const
    noexcept(is_nothrow_tag_invocable_v<transform_cpo, Sender, Func>)
    -> tag_invoke_result_t<transform_cpo, Sender, Func> {
    return unifex::tag_invoke(*this, (Sender&&)sender, (Func&&)func);
  }

  template<
    typename Sender,
    typename Func,
    std::enable_if_t<
      !is_tag_invocable_v<transform_cpo, Sender, Func>, int> = 0>
  auto operator()(Sender&& sender, Func&& func) const
    noexcept(std::is_nothrow_constructible_v<std::decay_t<Sender>, Sender> &&
             std::is_nothrow_constructible_v<std::decay_t<Func>, Func>)
    -> typename detail::transform_sender<std::decay_t<Sender>, std::decay_t<Func>>::type {
    return typename detail::transform_sender<std::decay_t<Sender>, std::decay_t<Func>>::type(
      (Sender&&)sender, (Func&&)func);
  }
} transform;

} // namespace unifex
