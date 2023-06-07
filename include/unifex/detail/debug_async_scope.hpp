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

#include <unifex/detail/intrusive_list.hpp>
#if __has_include(<cxxabi.h>)
#  define UNIFEX_NO_DEMANGLE 0
#  include <cxxabi.h>
#else
// TODO
// https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-undecoratesymbolname?redirectedfrom=MSDN
#  define UNIFEX_NO_DEMANGLE 1
#endif
#include <mutex>
#include <typeinfo>

#include <unifex/detail/prologue.hpp>

namespace unifex::detail {
namespace _debug_async_scope {

struct op_base {
  explicit op_base(const std::type_info& concreteType) noexcept
    : concreteType(concreteType) {
    demangled = demangle(concreteType);
  }

  const std::type_info& concreteType;
  const char* demangled;
  bool shouldFree{false};

  const char* demangle(const std::type_info& concreteType) noexcept {
#if !UNIFEX_NO_DEMANGLE
    int status = -1;
    auto result = abi::__cxa_demangle(concreteType.name(), nullptr, 0, &status);
    if (status == 0) {
      shouldFree = true;
      return result;
    }
    return concreteType.name();
#else
    return concreteType.name();
#endif
  }

  op_base* next{nullptr};
  op_base* prev{nullptr};

  ~op_base() {
    if (shouldFree) {
      std::free((void*)demangled);
    }
  }
};

using debug_ops_t = intrusive_list<op_base, &op_base::next, &op_base::prev>;

struct debug_op_list final {
  std::mutex mutex_;
  debug_ops_t ops_;

  void register_debug_operation(op_base* op) noexcept {
    std::lock_guard lock{mutex_};
    ops_.push_back(op);
  }

  void deregister_debug_operation(op_base* op) noexcept {
    std::lock_guard lock{mutex_};
    ops_.remove(op);
  }
};

template <typename Receiver>
struct _operation final {
  struct type;
};

template <typename Receiver>
struct _operation<Receiver>::type : op_base {
  template <typename Receiver2>
  explicit type(
      const std::type_info& t,
      debug_op_list* ops,
      Receiver2&& receiver) noexcept(std::
                                         is_nothrow_constructible_v<
                                             Receiver,
                                             Receiver2>)
    : op_base{t}
    , ops_(ops)
    , receiver_(static_cast<Receiver2&&>(receiver)) {}

  type(type&&) = delete;

  debug_op_list* ops_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  template <typename Func>
  void complete(Func func) noexcept {
    ops_->deregister_debug_operation(this);
    func(std::move(receiver_));
  }
};

template <typename Receiver>
struct _receiver final {
  struct type;
};

template <typename Receiver>
struct _receiver<Receiver>::type final {
  typename _operation<Receiver>::type* op_;

  template <typename... Values>
  void set_value(Values&&... values) noexcept {
    op_->complete([&](Receiver&& receiver) noexcept {
      UNIFEX_TRY {
        unifex::set_value(
            std::move(receiver), static_cast<Values&&>(values)...);
      }
      UNIFEX_CATCH(...) {
        unifex::set_error(std::move(receiver), std::current_exception());
      }
    });
  }

  template <typename E>
  void set_error(E&& e) noexcept {
    op_->complete([&e](Receiver&& receiver) noexcept {
      unifex::set_error(std::move(receiver), static_cast<E&&>(e));
    });
  }

  void set_done() noexcept { op_->complete(unifex::set_done); }

  template(typename CPO)                       //
      (requires is_receiver_query_cpo_v<CPO>)  //
      friend auto tag_invoke(CPO cpo, const type& r) noexcept(
          is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.op_->receiver_));
  }
};

template <typename Sender, typename Receiver>
struct _operation_impl final {
  struct type;
};

template <typename Sender, typename Receiver>
using debug_op =
    typename _operation_impl<Sender, remove_cvref_t<Receiver>>::type;

template <typename Sender, typename Receiver>
struct _operation_impl<Sender, Receiver>::type final
  : _operation<Receiver>::type {
  using base_op_t = typename _operation<Receiver>::type;
  using receiver_t = typename _receiver<Receiver>::type;

  template <typename Sender2, typename Receiver2>
  explicit type(
      debug_op_list* ops,
      Sender2&& sender,
      Receiver2&& receiver) noexcept(std::
                                         is_nothrow_constructible_v<
                                             base_op_t,
                                             const std::type_info&,
                                             debug_op_list*,
                                             Receiver2>&&
                                             is_nothrow_connectable_v<
                                                 Sender2,
                                                 receiver_t>)
    : base_op_t(
          typeid(connect_result_t<Sender, Receiver>),
          ops,
          static_cast<Receiver2&&>(receiver))
    , op_(unifex::connect(static_cast<Sender2&&>(sender), receiver_t{this})) {}

  type(type&&) = delete;

  friend void tag_invoke(tag_t<start>, type& op) noexcept {
    op.ops_->register_debug_operation(&op);
    unifex::start(op.op_);
  }

private:
  using op_t = connect_result_t<Sender, receiver_t>;
  op_t op_;
};

template <typename Sender>
struct _debug_scope_sender final {
  struct type;
};

template <typename Sender>
struct _debug_scope_sender<Sender>::type final {
  template <
      template <typename...>
      typename Variant,
      template <typename...>
      typename Tuple>
  using value_types = sender_value_types_t<Sender, Variant, Tuple>;

  template <template <typename...> typename Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Sender, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Sender>::sends_done;

  template <typename Sender2>
  explicit type(Sender2&& sender, debug_op_list* ops) noexcept(
      std::is_nothrow_constructible_v<Sender, Sender2>)
    : ops_(ops)
    , sender_(static_cast<Sender2&&>(sender)) {}

  template(typename Self, typename Receiver)          //
      (requires same_as<type, remove_cvref_t<Self>>)  //
      friend debug_op<Sender, Receiver> tag_invoke(
          tag_t<connect>,
          Self&& self,
          Receiver&& receiver) noexcept(std::
                                            is_nothrow_constructible_v<
                                                debug_op<Sender, Receiver>,
                                                debug_op_list*,
                                                member_t<Self, Sender>,
                                                Receiver>) {
    return debug_op<Sender, Receiver>{
        static_cast<Self&&>(self).ops_,
        static_cast<Self&&>(self).sender_,
        static_cast<Receiver&&>(receiver)};
  }

private:
  debug_op_list* ops_;
  UNIFEX_NO_UNIQUE_ADDRESS Sender sender_;
};

}  // namespace _debug_async_scope

using _debug_async_scope::debug_op_list;
template <typename Sender>
using debug_scope_sender =
    typename _debug_async_scope::_debug_scope_sender<Sender>::type;

}  // namespace unifex::detail

#include <unifex/detail/epilogue.hpp>
