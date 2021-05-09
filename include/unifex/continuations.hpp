/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <unifex/coroutine.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/tag_invoke.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _visit_continuations_cpo {
  inline const struct _fn {
#if !UNIFEX_NO_COROUTINES
    template(typename Promise, typename Func)
        (requires (!same_as<Promise, void>))
    friend void tag_invoke(
        _fn cpo,
        coro::coroutine_handle<Promise> h,
        Func&& func) {
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

class continuation_info {
 public:
  template <typename Continuation>
  static continuation_info from_continuation(const Continuation& c) noexcept;

  static continuation_info from_continuation(
      const continuation_info& c) noexcept {
    return c;
  }

  type_index type() const noexcept {
    return vtable_->typeIndexGetter_();
  }

  const void* address() const noexcept {
    return address_;
  }

  template <typename F>
  friend void
  tag_invoke(tag_t<visit_continuations>, const continuation_info& c, F&& f) {
    c.vtable_->visit_(
        c.address_,
        [](const continuation_info& info, void* data) {
          std::invoke(*static_cast<std::add_pointer_t<F>>(data), info);
        },
        static_cast<void*>(std::addressof(f)));
  }

 private:
  using callback_t = void(const continuation_info&, void*);
  using visitor_t = void(const void*, callback_t*, void*);
  using type_index_getter_t = type_index() noexcept;

  struct vtable_t {
    type_index_getter_t* typeIndexGetter_;
    visitor_t* visit_;
  };

  explicit continuation_info(
      const void* address,
      const vtable_t* vtable) noexcept
    : address_(address)
    , vtable_(vtable) {}

  const void* address_;
  const vtable_t* vtable_;
};

template <typename Continuation>
inline continuation_info continuation_info::from_continuation(
    const Continuation& r) noexcept {
  static constexpr vtable_t vtable{
      []() noexcept -> type_index {
        return type_id<remove_cvref_t<Continuation>>();
      },
      [](const void* address, callback_t* cb, void* data) {
        visit_continuations(
            *static_cast<const Continuation*>(address),
            [cb, data](const auto& continuation) {
              cb(continuation_info::from_continuation(continuation), data);
            });
      }};
  return continuation_info{static_cast<const void*>(std::addressof(r)),
                           &vtable};
}

#if !UNIFEX_NO_COROUTINES
// BUGBUG "inherit" this from continuation_info
template <typename Promise = void>
struct continuation_handle;

template <>
struct continuation_handle<void> {
private:
  [[noreturn]] static coro::coroutine_handle<> default_done_callback(void*) noexcept {
    std::terminate();
  }

  template <typename Promise>
  static coro::coroutine_handle<> forward_unhandled_done_callback(void* p) noexcept {
    return coro::coroutine_handle<Promise>::from_address(p).promise().unhandled_done();
  }

  using done_callback_t = coro::coroutine_handle<>(*)(void*) noexcept;

  coro::coroutine_handle<> handle_{};
  done_callback_t doneCallback_ = &default_done_callback;

public:
  continuation_handle() = default;

  template (typename Promise)
    (requires (!same_as<Promise, void>))
  /*implicit*/ continuation_handle(coro::coroutine_handle<Promise> continuation) noexcept
    : handle_((coro::coroutine_handle<Promise>&&) continuation)
    , doneCallback_(&forward_unhandled_done_callback<Promise>)
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
    return doneCallback_(handle_.address());
  }
};

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

private:
  continuation_handle<> self_;
};
#endif

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
