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
#include <unifex/get_allocator.hpp>
#include <unifex/nest.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tracing/async_stack.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/detail/unifex_fwd.hpp>

#include <exception>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _spawn_detached {

template <typename Alloc, bool WithAsyncStackSupport>
struct _spawn_detached_receiver final {
  struct type;
};

template <typename Alloc, bool WithAsyncStackSupport>
struct _spawn_detached_receiver<Alloc, WithAsyncStackSupport>::type final {
  static_assert(same_as<std::byte, typename Alloc::value_type>);

  void set_value() noexcept { deleter_(std::move(alloc_), op_); }

  [[noreturn]] void set_error(std::exception_ptr) noexcept { std::terminate(); }

  void set_done() noexcept { set_value(); }

  friend Alloc tag_invoke(tag_t<get_allocator>, const type& receiver) noexcept(
      std::is_nothrow_copy_constructible_v<Alloc>) {
    return receiver.alloc_;
  }

  template(typename Receiver)                                       //
      (requires WithAsyncStackSupport AND same_as<type, Receiver>)  //
      friend AsyncStackFrame* tag_invoke(
          tag_t<get_async_stack_frame>, const Receiver& receiver) noexcept {
    return reinterpret_cast<AsyncStackFrame*>(receiver.op_);
  }

  void* op_;
  void (*deleter_)(Alloc, void*) noexcept;
  UNIFEX_NO_UNIQUE_ADDRESS Alloc alloc_;
};

template <typename Alloc, bool WithAsyncStackSupport>
using spawn_detached_receiver_t = typename _spawn_detached_receiver<
    typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>,
    WithAsyncStackSupport>::type;

template <typename Sender, typename Alloc, bool WithAsyncStackSupport>
struct _spawn_detached_op final {
  struct type;
};

template <typename Sender, typename Alloc, bool WithAsyncStackSupport>
struct _spawn_detached_op<Sender, Alloc, WithAsyncStackSupport>::type final {
  static_assert(same_as<std::byte, typename Alloc::value_type>);

  explicit type(Sender&& sender, const Alloc& alloc, [[maybe_unused]] instruction_ptr returnAddress) noexcept(
      is_nothrow_connectable_v<
          Sender,
          spawn_detached_receiver_t<Alloc, WithAsyncStackSupport>>) {
    ::new (op_address()) op_t{connect(
        std::move(sender),
        spawn_detached_receiver_t<Alloc, WithAsyncStackSupport>{
            this, destroy, alloc})};

    if constexpr (WithAsyncStackSupport) {
      frame_.setReturnAddress(returnAddress);
      static_assert(std::is_standard_layout_v<type>);
    }
  }

  type(type&&) = delete;

  ~type() { op().~op_t(); }

  friend void tag_invoke(tag_t<start>, type& op) noexcept {
    unifex::start(op.op());
  }

private:
  using op_t = connect_result_t<
      Sender,
      spawn_detached_receiver_t<Alloc, WithAsyncStackSupport>>;

  // HACK: this is first to make the reinterpret_cast in the receiver work
  UNIFEX_NO_UNIQUE_ADDRESS
  std::conditional_t<WithAsyncStackSupport, AsyncStackFrame, detail::_empty<0>>
      frame_;
  alignas(op_t) std::byte op_[sizeof(op_t)];

  void* op_address() noexcept { return static_cast<void*>(op_); }

  op_t& op() noexcept { return *static_cast<op_t*>(op_address()); }

  static void destroy(Alloc alloc, void* p) noexcept {
    using allocator_t =
        typename std::allocator_traits<Alloc>::template rebind_alloc<type>;
    using traits_t = typename std::allocator_traits<allocator_t>;

    auto typed = static_cast<type*>(p);
    allocator_t allocator{std::move(alloc)};

    traits_t::destroy(allocator, typed);
    traits_t::deallocate(allocator, typed, 1);
  }
};

template <typename Sender, typename Alloc, bool WithAsyncStackSupport>
using spawn_detached_op_t = typename _spawn_detached_op<
    Sender,
    // standardize on a std::byte allocator to minimize template instantiations
    typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>,
    WithAsyncStackSupport>::type;

struct _spawn_detached_fn {
private:
  struct deref;

public:
  template(
      typename Sender,
      typename Scope,
      typename Alloc = std::allocator<std::byte>,
      bool WithAsyncStackSupport = !UNIFEX_NO_ASYNC_STACKS)  //
      (requires sender<Sender> AND is_allocator_v<Alloc> AND sender_to<
          decltype(nest(UNIFEX_DECLVAL(Sender), UNIFEX_DECLVAL(Scope&))),
          spawn_detached_receiver_t<Alloc, WithAsyncStackSupport>>)  //
      void
      operator()(Sender&& sender, Scope& scope, const Alloc& alloc = {}) const {
    using sender_t = decltype(nest(static_cast<Sender&&>(sender), scope));

    using op_t = spawn_detached_op_t<sender_t, Alloc, WithAsyncStackSupport>;

    using allocator_t =
        typename std::allocator_traits<Alloc>::template rebind_alloc<op_t>;
    using traits = std::allocator_traits<allocator_t>;

    allocator_t allocator{alloc};

    // this could throw std::bad_alloc, in which case we can let the exception
    // bubble out
    auto op = traits::allocate(allocator, 1);

    // prepare to deallocate what we just allocated in case of an exception in
    // the operation state constructor
    scope_guard g = [&]() noexcept {
      traits::deallocate(allocator, op, 1);
    };

    // once this succeeds, the operation owns itself; the allocate is balanced
    // by the deallocate in op_t::destroy
    traits::construct(
        allocator,
        op,
        nest(static_cast<Sender&&>(sender), scope),
        allocator,
        instruction_ptr::read_return_address());

    // since construction succeeded, don't deallocate the memory
    g.release();

    unifex::start(*op);
  }

  template <typename Scope, typename Alloc = std::allocator<std::byte>>
  constexpr auto
  operator()(Scope& scope, const Alloc& alloc = {}) const noexcept(
      std::
          is_nothrow_invocable_v<tag_t<bind_back>, deref, Scope*, const Alloc&>)
      -> std::enable_if_t<
          is_allocator_v<Alloc>,
          bind_back_result_t<deref, Scope*, const Alloc&>>;
};

struct _spawn_detached_fn::deref final {
  template <typename Sender, typename Scope, typename Alloc>
  auto operator()(Sender&& sender, Scope* scope, const Alloc& alloc) const
      -> decltype(_spawn_detached_fn{}(
          static_cast<Sender&&>(sender), *scope, alloc)) {
    return _spawn_detached_fn{}(static_cast<Sender&&>(sender), *scope, alloc);
  }
};

template <typename Scope, typename Alloc>
inline constexpr auto
_spawn_detached_fn::operator()(Scope& scope, const Alloc& alloc) const noexcept(
    std::is_nothrow_invocable_v<tag_t<bind_back>, deref, Scope*, const Alloc&>)
    -> std::enable_if_t<
        is_allocator_v<Alloc>,
        bind_back_result_t<deref, Scope*, const Alloc&>> {
  return bind_back(deref{}, &scope, alloc);
}

}  // namespace _spawn_detached

inline constexpr _spawn_detached::_spawn_detached_fn spawn_detached{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
