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

#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/type_traits.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

// When started with start(outer), will call outer.forward_set_value() on the
// execution context obtained by scheduling on
// get_scheduler(outer.get_receiver()). If schedule() fails or is cancelled,
// will forward set_error()/set_done() to outer.get_receiver().
// outer.get_receiver() must return FinalReceiver&.
// outer.forward_set_value must not throw.
template <typename OpState, typename FinalReceiver>
class completion_forwarder {
public:
  ~completion_forwarder() noexcept {
    if (started_) {
      inner_.destruct();
    }
  }

  void start(OpState& outer) noexcept {
    static_assert(
        std::is_same_v<FinalReceiver&, decltype(outer.get_receiver())>);
    inner_.construct_with([&outer]() noexcept {
      return unifex::connect(
          schedule(get_scheduler(outer.get_receiver())), receiver{outer});
    });
    started_ = true;
    unifex::start(inner_.get());
  }

private:
  struct receiver {
    explicit receiver(OpState& outer) noexcept : outer_(outer) {}

    void set_value() noexcept {
      static_assert(noexcept(outer_.forward_set_value()));
      outer_.forward_set_value();
    }

    template <typename Error>
    void set_error(Error&& error) noexcept {
      unifex::set_error(
          std::move(outer_.get_receiver()), std::forward<Error>(error));
    }

    void set_done() noexcept {
      unifex::set_done(std::move(outer_.get_receiver()));
    }

    template(typename CPO)                       //
        (requires is_receiver_query_cpo_v<CPO>)  //
        friend auto tag_invoke(CPO cpo, const receiver& r) noexcept(
            std::is_nothrow_invocable_v<CPO, const FinalReceiver&>)
            -> std::invoke_result_t<CPO, const FinalReceiver&> {
      return std::move(cpo)(std::as_const(r.outer_.get_receiver()));
    }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
    template <typename Func>
    friend void
    tag_invoke(tag_t<visit_continuations>, const receiver& r, Func&& f) {
      std::invoke(f, r.outer_.get_receiver());
    }
#endif

    OpState& outer_;
  };

  using inner_opstate = std::decay_t<decltype(unifex::connect(
      schedule(get_scheduler(UNIFEX_DECLVAL(FinalReceiver&))),
      UNIFEX_DECLVAL(receiver)))>;

  manual_lifetime<inner_opstate> inner_;
  bool started_{false};
};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
