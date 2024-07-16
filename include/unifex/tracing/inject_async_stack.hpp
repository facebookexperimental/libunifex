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
#include <unifex/receiver_concepts.hpp>
#include <unifex/tracing/async_stack.hpp>
#include <unifex/tracing/get_async_stack_frame.hpp>
#include <unifex/tracing/get_return_address.hpp>
#include <unifex/detail/unifex_fwd.hpp>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _inject {

struct _op_base {
  explicit _op_base(instruction_ptr returnAddress) noexcept {
    frame_.setReturnAddress(returnAddress);
  }

  _op_base(_op_base&&) = delete;

  AsyncStackFrame frame_;

protected:
  ~_op_base() = default;
};

template <typename Receiver>
struct _op_with_receiver final {
  struct type;
};

template <typename Receiver>
struct _op_with_receiver<Receiver>::type : _op_base {
  template <typename Receiver2>
  explicit type(instruction_ptr returnAddress, Receiver2&& receiver) noexcept(
      std::is_nothrow_constructible_v<Receiver, Receiver2>)
    : _op_base{returnAddress}
    , receiver_{std::forward<Receiver2>(receiver)} {}

  type(type&&) = delete;

  Receiver receiver_;

protected:
  ~type() = default;
};

template <typename Receiver>
using op_with_receiver_t = typename _op_with_receiver<Receiver>::type;

struct _root_and_frame {
  UNIFEX_NO_INLINE explicit _root_and_frame(AsyncStackFrame* frame) noexcept {
    if (frame) {
      if (auto* parent = frame->getParentFrame()) {
        frame_.setParentFrame(*parent);
      }

      frame_.setReturnAddress(frame->getReturnAddress());
    }
    root_.activateFrame(frame_);
  }

  _root_and_frame(_root_and_frame&&) = delete;

  ~_root_and_frame() { deactivateAsyncStackFrame(frame_); }

private:
  AsyncStackFrame frame_;
  unifex::detail::ScopedAsyncStackRoot root_;
};

struct _root_and_frame_ref {
  UNIFEX_NO_INLINE explicit _root_and_frame_ref(
      AsyncStackFrame& frame, AsyncStackFrame* parentFrame) noexcept
    : frame_(&frame) {
    if (parentFrame) {
      frame_->setParentFrame(*parentFrame);
    }

    root_.activateFrame(*frame_);
  }

  _root_and_frame_ref(_root_and_frame_ref&&) = delete;

  ~_root_and_frame_ref() { root_.ensureFrameDeactivated(frame_); }

private:
  AsyncStackFrame* frame_;
  unifex::detail::ScopedAsyncStackRoot root_;
};

struct _rcvr_base {
  _op_base* op_;

  friend constexpr AsyncStackFrame*
  tag_invoke(tag_t<get_async_stack_frame>, const _rcvr_base& r) noexcept {
    return &r.op_->frame_;
  }
};

template <typename Receiver>
struct _rcvr_wrapper final {
  struct type;
};

template <typename Receiver>
struct _rcvr_wrapper<Receiver>::type final : _rcvr_base {
  template <typename... T>
  void set_value(T&&... ts) noexcept {
    _root_and_frame rf(get_async_stack_frame(receiver()));

    UNIFEX_TRY {
      unifex::set_value(std::move(receiver()), std::forward<T>(ts)...);
    }
    UNIFEX_CATCH(...) {
      unifex::set_error(std::move(receiver()), std::current_exception());
    }
  }

  template <typename... T>
  void
  set_next(T&&... ts) noexcept(is_nothrow_next_receiver_v<Receiver, T...>) {
    _root_and_frame rf(get_async_stack_frame(receiver()));

    unifex::set_next(receiver(), std::forward<T>(ts)...);
  }

  template <typename E>
  void set_error(E&& e) noexcept {
    _root_and_frame rf(get_async_stack_frame(receiver()));

    unifex::set_error(std::move(receiver()), std::forward<E>(e));
  }

  void set_done() noexcept {
    _root_and_frame rf(get_async_stack_frame(receiver()));

    unifex::set_done(std::move(receiver()));
  }

  template(typename CPO, typename R)  //
      (requires(!same_as<CPO, tag_t<get_async_stack_frame>>)
           AND is_receiver_query_cpo_v<CPO> AND same_as<R, type>)  //
      friend auto tag_invoke(CPO cpo, const R& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver()));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Visit>
  friend void
  tag_invoke(tag_t<visit_continuations>, const type& r, Visit&& visit) {
    std::invoke(visit, r.receiver());
  }
#endif

  Receiver& receiver() const noexcept {
    return static_cast<op_with_receiver_t<Receiver>*>(this->op_)->receiver_;
  }
};

template <typename R>
using receiver_t = typename _rcvr_wrapper<remove_cvref_t<R>>::type;

template <typename Op, typename R>
struct _op_wrapper final {
  struct type;
};

template <typename Op, typename R>
struct _op_wrapper<Op, R>::type final
  : _op_with_receiver<remove_cvref_t<R>>::type {
  template(typename S, typename Fn)                         //
      (requires std::is_invocable_v<Fn, S, receiver_t<R>>)  //
      explicit type(S&& s, R&& r, Fn&& fn) noexcept(
          std::is_nothrow_invocable_v<Fn, S, receiver_t<R>> &&
          std::is_nothrow_constructible_v<remove_cvref_t<R>, R>)
    : _op_with_receiver<
          remove_cvref_t<R>>::type{get_return_address(s), std::forward<R>(r)}
    , op_{std::forward<Fn>(fn)(std::forward<S>(s), receiver_t<R>{{this}})} {}

  type(type&&) = delete;

  ~type() = default;

  UNIFEX_NO_INLINE void start() & noexcept {
    _root_and_frame_ref rf{
        this->frame_, get_async_stack_frame(this->receiver_)};

    unifex::start(op_);
  }

private:
  Op op_;
};

template <typename Op, typename R>
using op_wrapper = typename _op_wrapper<Op, R>::type;

template <typename S, typename R, typename Fn>
auto make_op_wrapper(S&& s, R&& r, Fn&& fn) noexcept(
    std::is_nothrow_constructible_v<
        op_wrapper<std::invoke_result_t<Fn, S, receiver_t<R>>, R>,
        S,
        R,
        Fn>) -> op_wrapper<std::invoke_result_t<Fn, S, receiver_t<R>>, R> {
  return op_wrapper<std::invoke_result_t<Fn, S, receiver_t<R>>, R>{
      std::forward<S>(s), std::forward<R>(r), std::forward<Fn>(fn)};
}

}  // namespace _inject

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
