/*
 * Copyright 2019-present Facebook, Inc.
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

#include <unifex/blocking.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/config.hpp>

#if UNIFEX_HAVE_COROUTINES
#include <experimental/coroutine>
#endif

#include <functional>
#include <typeindex>
#include <vector>

namespace unifex {

inline constexpr struct visit_continuations_cpo {
  template <typename Continuation, typename Func>
  friend void
  tag_invoke(visit_continuations_cpo, const Continuation&, Func&&) noexcept {}

#if UNIFEX_HAVE_COROUTINES
  template <
      typename Promise,
      typename Func,
      std::enable_if_t<!std::is_void_v<Promise>, int> = 0>
  friend void tag_invoke(
      visit_continuations_cpo cpo,
      std::experimental::coroutine_handle<Promise> h,
      Func&& func) {
    cpo(h.promise(), (Func &&) func);
  }
#endif // UNIFEX_HAVE_COROUTINES

  template <typename Continuation, typename Func>
  void operator()(const Continuation& c, Func&& func) const
      noexcept(is_nothrow_tag_invocable_v<
               visit_continuations_cpo,
               const Continuation&,
               Func&&>) {
    static_assert(
        std::is_void_v<tag_invoke_result_t<
            visit_continuations_cpo,
            const Continuation&,
            Func&&>>,
        "tag_invoke() overload for visit_continuations() must return void");
    return tag_invoke(visit_continuations_cpo{}, c, (Func &&) func);
  }
} visit_continuations;

class continuation_info {
 public:
  template <typename Continuation>
  static continuation_info from_continuation(const Continuation& c) noexcept;

  static continuation_info from_continuation(
      const continuation_info& c) noexcept {
    return c;
  }

  std::type_index type() const noexcept {
    return vtable_->type_;
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

  struct vtable_t {
    const std::type_info& type_;
    visitor_t* visit_;
  };

  explicit continuation_info(
      const void* address,
      const vtable_t* vtable) noexcept
      : address_(address), vtable_(vtable) {}

  const void* address_;
  const vtable_t* vtable_;
};

template <typename Continuation>
continuation_info continuation_info::from_continuation(
    const Continuation& r) noexcept {
  static constexpr vtable_t vtable{
      typeid(std::remove_cvref_t<Continuation>),
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

struct async_trace_entry {
  async_trace_entry(
      std::size_t depth,
      std::size_t parentIndex,
      const continuation_info& continuation) noexcept
      : depth(depth), parentIndex(parentIndex), continuation(continuation) {}

  std::size_t depth;
  std::size_t parentIndex;
  continuation_info continuation;
};

template <typename Continuation>
std::vector<async_trace_entry> async_trace(const Continuation& c) {
  std::vector<async_trace_entry> results;
  results.emplace_back(0, 0, continuation_info::from_continuation(c));

  // Breadth-first search of async call-stack graph.
  for (std::size_t i = 0; i < results.size(); ++i) {
    auto [depth, parentIndex, info] = results[i];
    visit_continuations(
        info, [depth = depth, i, &results](const continuation_info& x) {
          results.emplace_back(depth + 1, i, x);
        });
  }

  return results;
}

class async_trace_sender {
 public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<std::vector<async_trace_entry>>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

 private:
  template <typename Receiver>
  struct operation {
    Receiver receiver_;

    void start() noexcept {
      try {
        auto trace = async_trace(receiver_);
        cpo::set_value(std::move(receiver_), std::move(trace));
      } catch (...) {
        cpo::set_error(std::move(receiver_), std::current_exception());
      }
    }
  };

 public:
  template <typename Receiver>
  operation<std::remove_cvref_t<Receiver>> connect(Receiver&& r) {
    return operation<std::remove_cvref_t<Receiver>>{(Receiver &&) r};
  }

  friend blocking_kind tag_invoke(
      tag_t<cpo::blocking>,
      const async_trace_sender&) noexcept {
    return blocking_kind::always_inline;
  }
};

} // namespace unifex
