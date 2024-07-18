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

#include <unifex/bind_back.hpp>
#include <unifex/blocking.hpp>
#include <unifex/exception.hpp>
#include <unifex/manual_event_loop.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tracing/async_stack.hpp>
#include <unifex/tracing/get_async_stack_frame.hpp>
#include <unifex/with_query_value.hpp>

#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _sync_wait {

template <typename T>
struct promise {
  promise() {}

  ~promise() {
    if (state_ == state::value) {
      unifex::deactivate_union_member(value_);
    } else if (state_ == state::error) {
      unifex::deactivate_union_member(exception_);
    }
  }
  union {
    manual_lifetime<T> value_;
    manual_lifetime<std::exception_ptr> exception_;
  };

  enum class state { incomplete, done, value, error };
  state state_ = state::incomplete;
};

template <typename T>
struct _receiver {
  struct type {
    promise<T>& promise_;
    manual_event_loop& ctx_;
    AsyncStackFrame& frame_;

    template <typename... Values>
    void set_value(Values&&... values) && noexcept {
      UNIFEX_TRY {
        unifex::activate_union_member(promise_.value_, (Values&&)values...);
        promise_.state_ = promise<T>::state::value;
      }
      UNIFEX_CATCH(...) {
        unifex::activate_union_member(
            promise_.exception_, std::current_exception());
        promise_.state_ = promise<T>::state::error;
      }

      signal_complete();
    }

    void set_error(std::exception_ptr err) && noexcept {
      unifex::activate_union_member(promise_.exception_, std::move(err));
      promise_.state_ = promise<T>::state::error;
      signal_complete();
    }

    void set_error(std::error_code ec) && noexcept {
      std::move(*this).set_error(
          make_exception_ptr(std::system_error{ec, "sync_wait"}));
    }

    template <typename Error>
    void set_error(Error&& e) && noexcept {
      std::move(*this).set_error(make_exception_ptr((Error&&)e));
    }

    void set_done() && noexcept {
      promise_.state_ = promise<T>::state::done;
      signal_complete();
    }

    friend auto tag_invoke(tag_t<get_scheduler>, const type& r) noexcept {
      return r.ctx_.get_scheduler();
    }

    friend constexpr AsyncStackFrame*
    tag_invoke(tag_t<get_async_stack_frame>, const type& r) noexcept {
      return &r.frame_;
    }

  private:
    void signal_complete() noexcept { ctx_.stop(); }
  };
};

template <typename T>
using receiver_t = typename _receiver<T>::type;

struct initial_stack_root {
  explicit initial_stack_root(
      frame_ptr frameAddress, instruction_ptr returnAddress) noexcept
    : root{frameAddress, returnAddress} {
    frame.setReturnAddress(returnAddress);

    root.activateFrame(frame);
  }

  ~initial_stack_root() { deactivateAsyncStackFrame(frame); }

  AsyncStackFrame frame;
  unifex::detail::ScopedAsyncStackRoot root;
};

template <typename Result, typename Sender>
UNIFEX_CLANG_DISABLE_OPTIMIZATION std::optional<Result>
_impl(Sender&& sender, frame_ptr frameAddress, instruction_ptr returnAddress) {
  using promise_t = _sync_wait::promise<Result>;
  promise_t promise;
  manual_event_loop ctx;

  {
    initial_stack_root stackRoot{frameAddress, returnAddress};

    // Store state for the operation on the stack.
    auto operation = connect(
        (Sender&&)sender,
        _sync_wait::receiver_t<Result>{promise, ctx, stackRoot.frame});

    start(operation);

    ctx.run();
  }

  switch (promise.state_) {
    case promise_t::state::done: return std::nullopt;
    case promise_t::state::value: return std::move(promise.value_).get();
    case promise_t::state::error:
      std::rethrow_exception(promise.exception_.get());
    default: std::terminate();
  }
}
}  // namespace _sync_wait

namespace _sync_wait_cpo {
class _fn {
  struct impl_fn {
    template <typename Sender>
    auto operator()(
        Sender&& sender,
        frame_ptr frameAddress,
        instruction_ptr returnAddress) const
        -> std::optional<sender_single_value_result_t<remove_cvref_t<Sender>>> {
      using Result = sender_single_value_result_t<remove_cvref_t<Sender>>;
      return _sync_wait::_impl<Result>(
          std::forward<Sender>(sender), frameAddress, returnAddress);
    }
  };

public:
  template(typename Sender)      //
      (requires sender<Sender>)  //
      auto
      operator()(Sender&& sender) const
      -> std::optional<sender_single_value_result_t<remove_cvref_t<Sender>>> {
    return impl_fn{}(
        std::forward<Sender>(sender),
        frame_ptr::read_frame_pointer(),
        instruction_ptr::read_return_address());
  }

  // Not constexpr anymore because __builtin_frame_address(0) (and, presumably,
  // __builtin_return_address(0)) isn't constexpr in Clang constexpr
  auto operator()() const noexcept(std::is_nothrow_invocable_v<
                                   tag_t<bind_back>,
                                   _fn,
                                   frame_ptr,
                                   instruction_ptr>)
      -> bind_back_result_t<impl_fn, frame_ptr, instruction_ptr> {
    return bind_back(
        impl_fn{},
        frame_ptr::read_frame_pointer(),
        instruction_ptr::read_return_address());
  }
};
}  // namespace _sync_wait_cpo

inline constexpr _sync_wait_cpo::_fn sync_wait{};

namespace _sync_wait_r_cpo {
template <typename Result>
struct _fn {
  template(typename Sender)      //
      (requires sender<Sender>)  //
      decltype(auto)
      operator()(Sender&& sender) const {
    using Result2 = non_void_t<wrap_reference_t<decay_rvalue_t<Result>>>;
    return _sync_wait::_impl<Result2>(
        (Sender&&)sender,
        frame_ptr::read_frame_pointer(),
        instruction_ptr::read_return_address());
  }
};
}  // namespace _sync_wait_r_cpo

template <typename Result>
inline constexpr _sync_wait_r_cpo::_fn<Result> sync_wait_r{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
