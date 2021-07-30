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
#include <unifex/detail/unifex_fwd.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/submit.hpp>

#include <exception>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _execute {
template <typename F, typename S>
struct _as_receiver {
  F f_;
  void set_value() noexcept(is_nothrow_callable_v<F&>) {
    f_();
  }
  [[noreturn]] void set_error(std::exception_ptr) noexcept {
    std::terminate();
  }
  void set_done() noexcept {}
};

namespace _cpo {
  template <typename Fn>
  UNIFEX_CONCEPT //
    _lvalue_callable = //
      callable<remove_cvref_t<Fn>&> &&
      constructible_from<remove_cvref_t<Fn>, Fn> &&
      move_constructible<remove_cvref_t<Fn>>;

  struct _fn {
    template(typename Scheduler, typename Fn)
      (requires _lvalue_callable<Fn> AND
          scheduler<Scheduler> AND
          tag_invocable<_fn, Scheduler, Fn>)
    void operator()(Scheduler&& sched, Fn&& fn) const
        noexcept(is_nothrow_tag_invocable_v<_fn, Scheduler, Fn>) {
      unifex::tag_invoke(_fn{}, (Scheduler &&) sched, (Fn &&) fn);
    }
    template(typename Scheduler, typename Fn)
      (requires _lvalue_callable<Fn> AND
          scheduler<Scheduler> AND
          (!tag_invocable<_fn, Scheduler, Fn>))
    void operator()(Scheduler&& sched, Fn&& fn) const {
      using Receiver =
          _as_receiver<remove_cvref_t<Fn>, Scheduler>;
      auto snd = unifex::schedule((Scheduler&&) sched);
      unifex::submit(std::move(snd), Receiver{(Fn&&) fn});
    }
  };
} // namespace _cpo
} // namespace _execute

inline constexpr _execute::_cpo::_fn execute {};
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
