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

struct any_scheduler;

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

using any_scheduler_impl =
  any_unique_t<
    overload<any_sender_of<>(this_ const&)>(schedule),
    copy_as<any_scheduler>,
    get_type_id,
    overload<bool(const this_&, const any_scheduler&)>(equal_to)>;

struct any_scheduler {
  template (typename Scheduler)
    (requires (!same_as<Scheduler, any_scheduler>) AND scheduler<Scheduler>)
  /* implicit */ any_scheduler(Scheduler sched)
    : impl_((Scheduler&&) sched) {}

  any_scheduler(any_scheduler&&) noexcept = default;
  any_scheduler(const any_scheduler& that)
    : impl_(copy_as<any_scheduler>(that.impl_).impl_) {}

  any_scheduler& operator=(any_scheduler&&) noexcept = default;
  any_scheduler& operator=(const any_scheduler& that) {
    impl_ = copy_as<any_scheduler>(that.impl_).impl_;
    return *this;
  }

  any_sender_of<> schedule() const {
    return unifex::schedule(impl_);
  }

private:
  friend equal_to_fn;
  friend bool operator==(const any_scheduler& left, const any_scheduler& right) {
    return equal_to(left.impl_, right);
  }
  friend bool operator!=(const any_scheduler& left, const any_scheduler& right) {
    return !equal_to(left.impl_, right);
  }

  any_scheduler_impl impl_;
};

} // namespace _any_sched

using _any_sched::any_scheduler;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
