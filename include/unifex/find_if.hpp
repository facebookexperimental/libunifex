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
#include <unifex/just.hpp>
#include <unifex/execution_policy.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/transform.hpp>
#include <unifex/type_list.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/bulk_join.hpp>
#include <unifex/bulk_transform.hpp>
#include <unifex/bulk_schedule.hpp>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>
#include <memory>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _find_if {

template <typename Predecessor, typename Receiver, typename Func, typename FuncPolicy>
struct _receiver {
  struct type;
};
template <typename Predecessor, typename Receiver, typename Func, typename FuncPolicy>
using receiver_t = typename _receiver<Predecessor, Receiver, Func, FuncPolicy>::type;

template <typename Predecessor, typename Receiver, typename Func, typename FuncPolicy>
struct _operation_state;

template <typename Predecessor, typename Receiver, typename Func, typename FuncPolicy>
struct _receiver<Predecessor, Receiver, Func, FuncPolicy>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  // NO_UNIQUE_ADDRESS here triggers what appears to be a layout bug
  /*UNIFEX_NO_UNIQUE_ADDRESS*/ FuncPolicy funcPolicy_;
  _operation_state<Predecessor, Receiver, Func, FuncPolicy>& operation_state_;

  // Helper receiver type to unpack a tuple
  template<typename OutputReceiver>
  struct unpack_receiver {
    OutputReceiver output_receiver_;
    _operation_state<Predecessor, Receiver, Func, FuncPolicy>& operation_state_;

    template<typename Tuple, size_t... Idx>
    void unpack_helper(OutputReceiver&& output_receiver, Tuple&& t, std::index_sequence<Idx...>) {
      unifex::set_value(
        (OutputReceiver&&) output_receiver,
        std::move(std::get<Idx>(t))...);
    }

    template <typename Iterator, typename... Values>
    void set_value(std::tuple<Iterator, Values...>&& packedResult) && noexcept {
      operation_state_.cleanup();
      try {
        unpack_helper(
          (OutputReceiver&&) output_receiver_,
          std::move(packedResult),
          std::make_index_sequence<std::tuple_size_v<std::tuple<Iterator, Values...>>>{});
      } catch(...) {
        unifex::set_error((OutputReceiver&&)output_receiver_, std::current_exception());
      }
    }

    template <typename Error>
    void set_error(Error&& error) && noexcept {
      operation_state_.cleanup();
      unifex::set_error((OutputReceiver &&) output_receiver_, (Error &&) error);
    }

    void set_done() && noexcept {
      auto output_receiver = (OutputReceiver&&) output_receiver_;
      operation_state_.cleanup();
      unifex::set_done((OutputReceiver &&) output_receiver);
    }

    template(typename CPO, typename R)
        (requires is_receiver_query_cpo_v<CPO> AND same_as<R, unpack_receiver<OutputReceiver>>)
    friend auto tag_invoke(CPO cpo, const R& r) noexcept(
        is_nothrow_callable_v<CPO, const OutputReceiver&>)
        -> callable_result_t<CPO, const OutputReceiver&> {
      return std::move(cpo)(std::as_const(r.output_receiver_));
    }
  };

  struct find_if_helper {
    Func func_;

    template<typename Iterator, typename... Values>
    auto operator()(const unpack_receiver<Receiver>& /*unused*/, const sequenced_policy&, Iterator begin_it, Iterator end_it, Values&&... values) noexcept {
      // Sequential implementation
      return unifex::transform(
          unifex::just(std::forward<Values>(values)...),
          [this, begin_it, end_it](auto... values) {
            for(auto it = begin_it; it != end_it; ++it) {
              if(std::invoke((Func &&) func_, *it, values...)) {
                return std::tuple<Iterator, Values...>(it, std::move(values)...);
              }
            }
            return std::tuple<Iterator, Values...>(end_it, std::move(values)...);
          }
        );
    }

    template<typename Iterator, typename... Values>
    auto operator()(const unpack_receiver<Receiver>& receiver, const parallel_policy&, Iterator begin_it, Iterator end_it, Values&&... values) noexcept {
      // func_ is safe to run concurrently so let's make use of that

      // NOTE: Assumes random access iterator for now, on the assumption that the policy was accurate
      constexpr int max_num_chunks = 16;
      constexpr int min_chunk_size = 8;
      auto distance = std::distance(begin_it, end_it);
      auto num_chunks = (distance/max_num_chunks) > min_chunk_size ? max_num_chunks : ((distance+min_chunk_size)/min_chunk_size);
      auto chunk_size = (distance+num_chunks)/num_chunks;

      // Use bulk_schedule to construct parallelism, but block and use local vector for now
      return unifex::let(
        unifex::just(std::vector<Iterator>(num_chunks), std::forward<Values>(values)...),
        [this, sched = unifex::get_scheduler(receiver), begin_it, chunk_size, end_it, num_chunks](
            std::vector<Iterator>& perChunkState, Values&... values) {
          return unifex::transform(
            unifex::bulk_join(
              unifex::bulk_transform(
                unifex::bulk_schedule(std::move(sched), num_chunks),
                [this, &perChunkState, begin_it, chunk_size, end_it, num_chunks, &values...](std::size_t index){
                  auto chunk_begin_it = begin_it + (chunk_size*index);
                  auto chunk_end_it = chunk_begin_it;
                  if(index < (num_chunks-1)) {
                    std::advance(chunk_end_it, chunk_size);
                  } else {
                    chunk_end_it = end_it;
                  }

                  for(auto it = chunk_begin_it; it != chunk_end_it; ++it) {
                    if(std::invoke(func_, *it, values...)) {
                      perChunkState[index] = it;
                      return;
                    }
                  }
                  // If not found, return the very end value
                  perChunkState[index] = end_it;
                },
                unifex::par
              )
            ),
            [&perChunkState, end_it, &values...]() mutable -> std::tuple<Iterator, Values...> {
              for(auto it : perChunkState) {
                if(it != end_it) {
                  return std::tuple<Iterator, Values...>(it, std::move(values)...);
                }
              }
              return std::tuple<Iterator, Values...>(end_it, std::move(values)...);
            }
          );
        });
    }
  };

  template <typename Iterator, typename... Values>
  void set_value(Iterator begin_it, Iterator end_it, Values&&... values) && noexcept;

  template <typename Error>
  void set_error(Error&& error) && noexcept {
    unifex::set_error((Receiver &&) receiver_, (Error &&) error);
  }

  void set_done() && noexcept {
    unifex::set_done((Receiver &&) receiver_);
  }

  template(typename CPO, typename R)
      (requires is_receiver_query_cpo_v<CPO> AND same_as<R, type>)
  friend auto tag_invoke(CPO cpo, const R& r) noexcept(
      is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

  template <typename Visit>
  friend void tag_invoke(tag_t<visit_continuations>, const type& r, Visit&& visit) {
    std::invoke(visit, r.receiver_);
  }
};

template <typename Predecessor, typename Receiver, typename Func, typename FuncPolicy>
struct _operation_state {
  using receiver_type = receiver_t<Predecessor, Receiver, Func, FuncPolicy>;

  template <typename... Ts>
  struct find_if_apply {
    using type = connect_result_t<
      std::invoke_result_t<
        typename receiver_type::find_if_helper,
        typename receiver_type::template unpack_receiver<Receiver>,
        FuncPolicy,
        Ts&&...>,
      typename receiver_type::template unpack_receiver<Receiver>>;
  };
  using operation_state_t =
      typename Predecessor::template value_types<concat_type_lists_unique_t, find_if_apply>::type;

  template<typename InPlaceConstruct>
  _operation_state(InPlaceConstruct f) : predOp_{f(*this)} {
  }

  ~_operation_state() noexcept {
  }

  void start() noexcept {
    unifex::start(predOp_);
  }

  void startInner() noexcept {
    started_ = true;
    unifex::start(innerOp_.get());
  }

  void cleanup() noexcept {
    innerOp_.destruct();
  }

  connect_result_t<Predecessor, receiver_type> predOp_;
  manual_lifetime<operation_state_t> innerOp_;
  bool started_ = false;
};


template <typename Predecessor, typename Receiver, typename Func, typename FuncPolicy>
template <typename Iterator, typename... Values>
void _receiver<Predecessor, Receiver, Func, FuncPolicy>::type::set_value(
    Iterator begin_it, Iterator end_it, Values&&... values) && noexcept {
  unpack_receiver<Receiver> unpack{(Receiver &&) receiver_, operation_state_};
  try {
    auto find_if_implementation_sender = find_if_helper{std::move(func_)}(unpack, funcPolicy_, begin_it, end_it, (Values&&) values...);
    // Store nested operation state inside find_if's operation state
    operation_state_.innerOp_.construct_from([&]() mutable {
      return unifex::connect(std::move(find_if_implementation_sender), std::move(unpack));
    });

    operation_state_.startInner();
  } catch(...) {
    unifex::set_error(std::move(unpack), std::current_exception());
  }
}

template <typename Predecessor, typename Func, typename FuncPolicy>
struct _sender {
  struct type;
};
template <typename Predecessor, typename Func, typename FuncPolicy>
using sender_t = typename _sender<Predecessor, Func, FuncPolicy>::type;

template <typename Predecessor, typename Func, typename FuncPolicy>
struct _sender<Predecessor, Func, FuncPolicy>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS FuncPolicy funcPolicy_;

  template <typename BeginIt, typename EndIt, typename... Args>
  using result = type_list<type_list<BeginIt, Args...>>;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = type_list_nested_apply_t<
    typename Predecessor::template value_types<concat_type_lists_unique_t, result>,
    Variant,
    Tuple>;


  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
    typename Predecessor::template error_types<type_list>,
    type_list<std::exception_ptr>>::template apply<Variant>;

  template <typename Receiver>
  using receiver_type = receiver_t<Predecessor, Receiver, Func, FuncPolicy>;

  friend constexpr auto tag_invoke(tag_t<blocking>, const type& sender) {
    return blocking(sender.pred_);
  }

  template(typename Sender, typename Receiver)
    (requires same_as<remove_cvref_t<Sender>, type> AND receiver<Receiver>)
  friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r)
    noexcept(
      std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver> &&
      std::is_nothrow_constructible_v<Func, decltype((static_cast<Sender&&>(s).func_))> &&
      is_nothrow_connectable_v<decltype((static_cast<Sender&&>(s).pred_)), receiver_type<remove_cvref_t<Receiver>>>)
      -> _operation_state<Predecessor, Receiver, Func, FuncPolicy> {


    return _operation_state<Predecessor, Receiver, Func, FuncPolicy>{
      [&](_operation_state<Predecessor, Receiver, Func, FuncPolicy>& os){
        return unifex::connect(
          static_cast<Sender&&>(s).pred_,
          receiver_type<remove_cvref_t<Receiver>>{
            static_cast<Sender&&>(s).func_,
            static_cast<Receiver&&>(r),
            static_cast<Sender&&>(s).funcPolicy_,
            os
        });
      }
    };
  }
};
} // namespace _find_if

namespace _find_if_cpo {
  inline const struct _fn {
  public:
    template(typename Sender, typename Func, typename FuncPolicy)
      (requires tag_invocable<_fn, Sender, Func, FuncPolicy>)
    auto operator()(Sender&& predecessor, Func&& func, FuncPolicy policy) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Func, FuncPolicy>)
        -> tag_invoke_result_t<_fn, Sender, Func, FuncPolicy> {
      return unifex::tag_invoke(_fn{}, (Sender&&)predecessor, (Func&&)func, (FuncPolicy&&)policy);
    }
    template(typename Sender, typename Func, typename FuncPolicy)
      (requires (!tag_invocable<_fn, Sender, Func, FuncPolicy>))
    auto operator()(Sender&& predecessor, Func&& func, FuncPolicy policy) const
        noexcept(
        std::is_nothrow_constructible_v<remove_cvref_t<Sender>, Sender> &&
        std::is_nothrow_constructible_v<remove_cvref_t<Func>, Func> &&
        std::is_nothrow_constructible_v<remove_cvref_t<FuncPolicy>, FuncPolicy>)
        -> _find_if::sender_t<remove_cvref_t<Sender>, std::decay_t<Func>, FuncPolicy>{
      return _find_if::sender_t<remove_cvref_t<Sender>, std::decay_t<Func>, FuncPolicy>{
        (Sender &&) predecessor, (Func &&) func, (FuncPolicy &&) policy};
    }
  } find_if{};
} // namespace _find_if_cpo
using _find_if_cpo::find_if;
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
