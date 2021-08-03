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

#include <unifex/coroutine.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_index.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _visit_continuations_cpo {
  inline const struct _fn {
#if !UNIFEX_NO_COROUTINES
    template(typename Promise, typename Func)
        (requires (!same_as<Promise, void>) AND callable<_fn, Promise&, Func>)
    friend void tag_invoke(
        _fn cpo,
        coro::coroutine_handle<Promise> h,
        Func&& func) noexcept(is_nothrow_callable_v<_fn, Promise&, Func>) {
      cpo(h.promise(), (Func &&) func);
    }
#endif // UNIFEX_NO_COROUTINES

    template(typename Continuation, typename Func)
      (requires tag_invocable<_fn, const Continuation&, Func>)
    void operator()(const Continuation& c, Func&& func) const
        noexcept(is_nothrow_tag_invocable_v<
                _fn,
                const Continuation&,
                Func&&>) {
      static_assert(
          std::is_void_v<tag_invoke_result_t<
              _fn,
              const Continuation&,
              Func&&>>,
          "tag_invoke() overload for visit_continuations() must return void");
      return tag_invoke(_fn{}, c, (Func &&) func);
    }

    template(typename Continuation, typename Func)
      (requires (!tag_invocable<_fn, const Continuation&, Func>))
    void operator()(const Continuation&, Func&&) const noexcept {}
  } visit_continuations {};
} // namespace _visit_continuations_cpo
using _visit_continuations_cpo::visit_continuations;

#if !UNIFEX_NO_COROUTINES
namespace _ch {
template <typename Promise = void>
struct continuation_handle;
} // namespace _ch
using _ch::continuation_handle;
#endif

namespace _ci {
class continuation_info;

struct _continuation_info_vtable {
  using callback_t = void(const continuation_info&, void*);
  using visitor_t = void(const void*, callback_t*, void*);
  using type_index_getter_t = type_index() noexcept;

  type_index_getter_t* typeIndexGetter_;
  visitor_t* visit_;
};

inline type_index _default_type_index_getter() noexcept {
  return type_id<void>();
}

inline void _default_visit(const void*, _continuation_info_vtable::callback_t*, void*) {
}

template <typename F>
void _invoke_visitor(const continuation_info& info, void* data) {
  std::invoke(*static_cast<std::add_pointer_t<F>>(data), info);
}

class continuation_info {
 public:
  continuation_info() = default;

  template <typename Continuation>
  static continuation_info from_continuation(const Continuation& c) noexcept;

  static continuation_info from_continuation(const continuation_info& c) noexcept {
    return c;
  }

#if !UNIFEX_NO_COROUTINES
  template <typename Promise>
  static continuation_info from_continuation(
      const continuation_handle<Promise>& c) noexcept {
    return continuation_info{c.handle().address(), c.vtable_};
  }
#endif

  type_index type() const noexcept {
    return vtable_->typeIndexGetter_();
  }

  const void* address() const noexcept {
    return address_;
  }

  template <typename F>
  friend void
  tag_invoke(tag_t<visit_continuations>, const continuation_info& c, F&& f) {
    // Any const that gets stripped by the const_cast below gets reapplied by the
    // static_cast in _invoke_visitor.
    c.vtable_->visit_(
        c.address_,
        &_invoke_visitor<F>,
        const_cast<void*>(static_cast<const void*>(std::addressof(f))));
  }

 private:
  explicit continuation_info(
      const void* address,
      const _continuation_info_vtable* vtable) noexcept
    : address_(address)
    , vtable_(vtable) {}

  inline static constexpr _continuation_info_vtable default_vtable_ {
    &_default_type_index_getter,
    &_default_visit
  };

  const void* address_{nullptr};
  const _continuation_info_vtable* vtable_{&default_vtable_};
};

template <typename Continuation>
type_index _type_index_getter_for() noexcept {
  return type_id<Continuation>();
}

template <typename Continuation>
void _visit_for(const void* address, _continuation_info_vtable::callback_t* cb, void* data) {
  visit_continuations(
      *static_cast<const Continuation*>(address),
      [cb, data](const auto& continuation) {
        cb(continuation_info::from_continuation(continuation), data);
      });
}

template <typename Continuation>
inline constexpr _continuation_info_vtable _vtable_for {
  &_type_index_getter_for<Continuation>,
  &_visit_for<Continuation>
};

template <typename Continuation>
inline continuation_info continuation_info::from_continuation(
    const Continuation& r) noexcept {
  return continuation_info{static_cast<const void*>(std::addressof(r)),
                           &_vtable_for<Continuation>};
}
} // namespace _ci
using _ci::continuation_info;

#if !UNIFEX_NO_COROUTINES
namespace _ch {
[[noreturn]] inline coro::coroutine_handle<> _default_done_callback(void*) noexcept {
  std::terminate();
}

struct _continuation_handle_vtable : _ci::_continuation_info_vtable {
  using done_callback_t = coro::coroutine_handle<>(void*) noexcept;
  done_callback_t* doneCallback_;
};

template <>
struct continuation_handle<void> {
  continuation_handle() = default;

  // Because of a detail in the concepts emulation macros, it is not possible to
  // forward-declare a constrained function and provide its definition later. So
  // below we define a constrained constructor that trivially dispatches to an
  // unconstrained implementation defined elsewhere.
  template (typename Promise)
    (requires (!same_as<Promise, void>))
  /*implicit*/ continuation_handle(coro::coroutine_handle<Promise> continuation) noexcept
    : continuation_handle(0, (coro::coroutine_handle<Promise>&&) continuation)
  {}

  explicit operator bool() const noexcept {
    return handle_ != nullptr;
  }

  coro::coroutine_handle<> handle() const noexcept {
    return handle_;
  }

  void resume() {
    handle_.resume();
  }

  coro::coroutine_handle<> done() const noexcept {
    return vtable_->doneCallback_(handle_.address());
  }

  continuation_info info() const noexcept {
    return continuation_info::from_continuation(*this);
  }

  template <typename F>
  friend void
  tag_invoke(tag_t<visit_continuations>, const continuation_handle<>& c, F&& f) {
    // Any const that gets stripped by the const_cast below gets reapplied by the
    // static_cast in _invoke_visitor.
    c.vtable_->visit_(
        c.handle_.address(),
        &_ci::_invoke_visitor<F>,
        const_cast<void*>(static_cast<const void*>(std::addressof(f))));
  }

private:
  friend continuation_info;

  inline static constexpr _continuation_handle_vtable default_vtable_ {
    {
        &_ci::_default_type_index_getter,
        &_ci::_default_visit,
    },
    &_default_done_callback
  };

  template <typename Promise>
  continuation_handle(int, coro::coroutine_handle<Promise> continuation) noexcept;

  coro::coroutine_handle<> handle_{};
  const _continuation_handle_vtable* vtable_ {&default_vtable_};
};

template <typename Promise>
void _visit_for(const void* address, _ci::_continuation_info_vtable::callback_t* cb, void* data) {
  visit_continuations(
      const_cast<const Promise&>(
          coro::coroutine_handle<Promise>::from_address(
              const_cast<void*>(address)).promise()),
      [cb, data](const auto& continuation) {
        cb(continuation_info::from_continuation(continuation), data);
      });
}

template <typename Promise>
coro::coroutine_handle<> _done_callback_for(void* address) noexcept {
  return coro::coroutine_handle<Promise>::from_address(address).promise().unhandled_done();
}

template <typename Promise>
inline constexpr _continuation_handle_vtable _vtable_for {
  {
        &_ci::_type_index_getter_for<Promise>,
        &_visit_for<Promise>,
  },
  &_done_callback_for<Promise>
};

template <typename Promise>
continuation_handle<void>::continuation_handle(int, coro::coroutine_handle<Promise> continuation) noexcept
  : handle_((coro::coroutine_handle<Promise>&&) continuation)
  , vtable_(&_vtable_for<Promise>)
{}

template <typename Promise>
struct continuation_handle {
  continuation_handle() = default;

  /*implicit*/ continuation_handle(coro::coroutine_handle<Promise> continuation) noexcept
    : self_((coro::coroutine_handle<Promise>&&) continuation)
  {}

  explicit operator bool() const noexcept {
    return !!self_;
  }

  /*implicit*/ operator continuation_handle<>() const noexcept {
    return self_;
  }

  coro::coroutine_handle<Promise> handle() const noexcept {
    return coro::coroutine_handle<Promise>::from_address(
        self_.handle().address());
  }

  void resume() {
    self_.resume();
  }

  Promise& promise() const noexcept {
    return handle().promise();
  }

  coro::coroutine_handle<> done() const noexcept {
    return self_.done();
  }

  continuation_info info() const noexcept {
    return self_.info();
  }

  template <typename F>
  friend void
  tag_invoke(tag_t<visit_continuations>, const continuation_handle<Promise>& c, F&& f) {
    visit_continuations(c.self_, (F&&) f);
  }

private:
  friend continuation_info;

  continuation_handle<> self_;
};
} // namespace _ch
using _ch::continuation_handle;
#endif

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
