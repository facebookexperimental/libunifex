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
#include <unifex/type_traits.hpp>

#include <exception>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _spawn_detached {

template <typename Alloc>
struct _spawn_detached_receiver final {
  struct type;
};

template <typename Alloc>
struct _spawn_detached_receiver<Alloc>::type final {
  static_assert(same_as<std::byte, typename Alloc::value_type>);

  void set_value() noexcept { deleter_(std::move(alloc_), op_); }

  [[noreturn]] void set_error(std::exception_ptr) noexcept { std::terminate(); }

  void set_done() noexcept { set_value(); }

  friend Alloc tag_invoke(tag_t<get_allocator>, const type& receiver) noexcept(
      std::is_nothrow_copy_constructible_v<Alloc>) {
    return receiver.alloc_;
  }

  void* op_;
  void (*deleter_)(Alloc, void*) noexcept;
  UNIFEX_NO_UNIQUE_ADDRESS Alloc alloc_;
};

template <typename Alloc>
using spawn_detached_receiver_t =
    typename _spawn_detached_receiver<typename std::allocator_traits<
        Alloc>::template rebind_alloc<std::byte>>::type;

template <typename Sender, typename Alloc>
struct _spawn_detached_op final {
  struct type;
};

template <typename Sender, typename Alloc>
struct _spawn_detached_op<Sender, Alloc>::type final {
  static_assert(same_as<std::byte, typename Alloc::value_type>);

  explicit type(Sender&& sender, const Alloc& alloc) noexcept(
      is_nothrow_connectable_v<Sender, spawn_detached_receiver_t<Alloc>>)
    : op_{connect(
          std::move(sender),
          spawn_detached_receiver_t<Alloc>{this, destroy, alloc})} {}

  type(type&&) = delete;

  ~type() = default;

  friend void tag_invoke(tag_t<start>, type& op) noexcept {
    unifex::start(op.op_);
  }

private:
  using op_t = connect_result_t<Sender, spawn_detached_receiver_t<Alloc>>;

  op_t op_;

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

template <typename Sender, typename Alloc>
using spawn_detached_op_t = typename _spawn_detached_op<
    Sender,
    // standardize on a std::byte allocator to minimize template instantiations
    typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>>::
    type;

struct _spawn_detached_fn {
private:
  struct deref;

public:
  template(
      typename Sender,
      typename Scope,
      typename Alloc = std::allocator<std::byte>)  //
      (requires sender<Sender> AND                 //
           is_allocator_v<Alloc> AND               //
               sender_to<
                   decltype(nest(
                       UNIFEX_DECLVAL(Sender), UNIFEX_DECLVAL(Scope&))),
                   spawn_detached_receiver_t<Alloc>>)  //
      void
      operator()(Sender&& sender, Scope& scope, const Alloc& alloc = {}) const {
    using sender_t = decltype(nest(static_cast<Sender&&>(sender), scope));

    using op_t = spawn_detached_op_t<sender_t, Alloc>;

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
        allocator, op, nest(static_cast<Sender&&>(sender), scope), allocator);

    // since construction succeeded, don't deallocate the memory
    g.release();

    unifex::start(*op);
  }

  template <typename Scope, typename Alloc = std::allocator<std::byte>>
  constexpr auto operator()(Scope& scope, const Alloc& alloc = {}) const
      noexcept(
          is_nothrow_callable_v<tag_t<bind_back>, deref, Scope*, const Alloc&>)
          -> std::enable_if_t<
              is_allocator_v<Alloc>,
              bind_back_result_t<deref, Scope*, const Alloc&>>;
};

struct _spawn_detached_fn::deref final {
  template <typename Sender, typename Scope, typename Alloc>
  auto operator()(Sender&& sender, Scope* scope, const Alloc& alloc) const
      -> decltype(
          _spawn_detached_fn{}(static_cast<Sender&&>(sender), *scope, alloc)) {
    return _spawn_detached_fn{}(static_cast<Sender&&>(sender), *scope, alloc);
  }
};

template <typename Scope, typename Alloc>
inline constexpr auto
_spawn_detached_fn::operator()(Scope& scope, const Alloc& alloc) const noexcept(
    is_nothrow_callable_v<tag_t<bind_back>, deref, Scope*, const Alloc&>)
    -> std::enable_if_t<
        is_allocator_v<Alloc>,
        bind_back_result_t<deref, Scope*, const Alloc&>> {
  return bind_back(deref{}, &scope, alloc);
}

}  // namespace _spawn_detached

inline constexpr _spawn_detached::_spawn_detached_fn spawn_detached{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
