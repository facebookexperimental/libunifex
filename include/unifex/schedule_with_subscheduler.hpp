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

#include <unifex/scheduler_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/then.hpp>
#include <unifex/bind_back.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _subschedule {
  namespace _detail {
    template <typename T>
    struct _return_value {
      struct type {
        T value;

        T operator()() && {
          return std::move(value);
        }

        T operator()() & {
          return value;
        }

        T operator()() const& {
          return value;
        }
      };
    };
  } // _detail

  inline const struct _fn {
  private:
    template <typename T>
    using _return_value =
      typename _detail::_return_value<remove_cvref_t<T>>::type;

    template <typename Scheduler>
    using _default_result_t = _then::sender<
        callable_result_t<decltype(schedule), Scheduler&>,
        _return_value<Scheduler>>;
    template <typename Scheduler>
    using _result_t =
      typename conditional_t<
        tag_invocable<_fn, Scheduler>,
        meta_tag_invoke_result<_fn>,
        meta_quote1<_default_result_t>>::template apply<Scheduler>;
  public:
    template(typename Scheduler)
      (requires tag_invocable<_fn, Scheduler>)
    auto operator()(Scheduler&& sched) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Scheduler>)
        -> _result_t<Scheduler> {
      return unifex::tag_invoke(_fn{}, (Scheduler &&) sched);
    }
    template(typename Scheduler)
      (requires (!tag_invocable<_fn, Scheduler>))
    auto operator()(Scheduler&& sched) const
        -> _result_t<Scheduler> {
      auto&& scheduleOp = schedule(sched);
      return _result_t<Scheduler>{
        static_cast<decltype(scheduleOp)>(scheduleOp),
        {(Scheduler &&) sched}
      };
    }
    constexpr auto operator()() const
        noexcept(is_nothrow_callable_v<
          tag_t<bind_back>, _fn>)
        -> bind_back_result_t<_fn> {
      return bind_back(*this);
    }
  } schedule_with_subscheduler{};
} // namespace _subschedule

using _subschedule::schedule_with_subscheduler;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
