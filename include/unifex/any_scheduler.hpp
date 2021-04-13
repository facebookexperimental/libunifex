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

#include <unifex/any_unique.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/any_unique.hpp>
#include <unifex/any_sender_of.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/type_index.hpp>
#include <unifex/tag_invoke.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _any_sched {

template <typename Ret>
struct copy_as_fn {
  using type_erased_signature_t = Ret(const this_&);

  template <typename T>
  Ret operator()(const T& t) const {
    if constexpr (tag_invocable<copy_as_fn, const T&>) {
      return tag_invoke(*this, t);
    } else {
      return Ret{t};
    }
  }
};
template <typename Ret>
inline constexpr copy_as_fn<Ret> copy_as{};

inline constexpr struct get_type_id_fn {
  using type_erased_signature_t = type_index(const this_&) noexcept;

  template <typename T>
  type_index operator()(const T& x) const noexcept {
    if constexpr (tag_invocable<get_type_id_fn, const T&>) {
      return tag_invoke(*this, x);
    } else {
      return type_id<T>();
    }
  }
} get_type_id{};

inline constexpr struct equal_to_fn {
  template <typename T, typename U>
  bool operator()(const T& t, const U& u) const noexcept {
    if constexpr (tag_invocable<equal_to_fn, const T&, const U&>) {
      return tag_invoke(*this, t, u);
    } else {
      return type_id<T>() == get_type_id(u.impl_) &&
          t == *static_cast<const T*>(get_object_address(u.impl_));
    }
  }
} equal_to{};

template <typename... CPOs>
using _any_void_sender_of =
  typename _any::_with_receiver_queries<CPOs...>::template any_sender_of<>;

template <typename... CPOs>
using any_scheduler_impl =
  any_unique_t<
    overload<_any_void_sender_of<CPOs...>(this_ const&)>(schedule),
    copy_as<any_scheduler<CPOs...>>,
    get_type_id,
    overload<bool(const this_&, const any_scheduler<CPOs...>&)>(equal_to)>;

template <typename... CPOs>
struct _any_scheduler<CPOs...>::type {
  template (typename Scheduler)
    (requires (!same_as<Scheduler, type>) AND scheduler<Scheduler>)
  /* implicit */ type(Scheduler sched)
    : impl_((Scheduler&&) sched) {}

  type(type&&) noexcept = default;
  type(const type& that)
    : impl_(copy_as<type>(that.impl_).impl_) {}

  type& operator=(type&&) noexcept = default;
  type& operator=(const type& that) {
    impl_ = copy_as<type>(that.impl_).impl_;
    return *this;
  }

  _any_void_sender_of<CPOs...> schedule() const {
    return unifex::schedule(impl_);
  }

private:
  friend equal_to_fn;
  friend bool operator==(const type& left, const type& right) {
    return equal_to(left.impl_, right);
  }
  friend bool operator!=(const type& left, const type& right) {
    return !(left == right);
  }

  any_scheduler_impl<CPOs...> impl_;
};

} // namespace _any_sched

using any_scheduler = _any_sched::any_scheduler<>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
