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
struct _copy_as_fn {
  using type_erased_signature_t = Ret(const this_&);

  template (typename T)
    (requires tag_invocable<_copy_as_fn, T const&>)
  Ret operator()(T const& t) const {
    return tag_invoke(*this, t);
  }

  template (typename T)
    (requires (!tag_invocable<_copy_as_fn, T const&>) AND copy_constructible<T>)
  Ret operator()(T const& t) const {
    return Ret{t};
  }
};
template <typename Ret>
inline constexpr _copy_as_fn<Ret> _copy_as{};

inline constexpr struct _get_type_index_fn {
  using type_erased_signature_t = type_index(const this_&) noexcept;

  template <typename T>
  type_index operator()(T const& x) const noexcept {
    if constexpr (tag_invocable<_get_type_index_fn, T const&>) {
      return tag_invoke(*this, x);
    } else {
      return type_id<T>();
    }
  }
} _get_type_index{};

inline constexpr struct _equal_to_fn {
  template (typename T, typename U)
    (requires tag_invocable<_equal_to_fn, T const&, const U&>)
  bool operator()(T const& t, const U& u) const noexcept {
    static_assert(is_nothrow_tag_invocable_v<_equal_to_fn, T const&, const U&>);
    return tag_invoke(*this, t, u);
  }

  template (typename T, typename U)
    (requires (!tag_invocable<_equal_to_fn, T const&, const U&>) AND
      equality_comparable<T>)
  bool operator()(T const& t, const U& u) const noexcept {
    static_assert(noexcept(t == t),
        "Equality comparison of schedulers ought to be noexcept");
    return type_id<T>() == _get_type_index(u.impl_) &&
        t == *static_cast<T const*>(get_object_address(u.impl_));
  }
} _equal_to{};

template <typename... CPOs>
using _void_receiver_ref = _any::_receiver_ref<type_list<CPOs...>>;

template <typename... CPOs>
struct _schedule_and_connect_fn {
  struct type {
    using _rec_ref_t = _void_receiver_ref<CPOs...>;
    using type_erased_signature_t = _any::_operation_state(this_ const&, _rec_ref_t);

#ifdef _MSC_VER
    // MSVC (_MSC_VER == 1927) doesn't seem to like the requires
    // clause here. Use SFINAE instead.
    template <typename Scheduler>
    std::enable_if_t<
        is_tag_invocable_v<type, Scheduler const&, _rec_ref_t>,
        _any::_operation_state>
    operator()(Scheduler const& sched, _rec_ref_t rec) const {
      return tag_invoke(*this, sched, (_rec_ref_t&&) rec);
    }

    template <typename Scheduler>
    std::enable_if_t<
        !is_tag_invocable_v<type, Scheduler const&, _rec_ref_t>,
        _any::_operation_state>
    operator()(Scheduler const& sched, _rec_ref_t rec) const {
      return _any::_connect<type_list<CPOs...>>(schedule(sched), (_rec_ref_t&&) rec);
    }
#else
    template (typename Scheduler)
      (requires tag_invocable<type, Scheduler const&, _rec_ref_t>)
    _any::_operation_state operator()(Scheduler const& sched, _rec_ref_t rec) const {
      return tag_invoke(*this, sched, (_rec_ref_t&&) rec);
    }

    template (typename Scheduler)
      (requires (!tag_invocable<type, Scheduler const&, _rec_ref_t>) AND
        scheduler<Scheduler>)
    _any::_operation_state operator()(Scheduler const& sched, _rec_ref_t rec) const {
      return _any::_connect<type_list<CPOs...>>(schedule(sched), (_rec_ref_t&&) rec);
    }
#endif
  };
};

template <typename... CPOs>
inline constexpr typename _schedule_and_connect_fn<CPOs...>::type _schedule_and_connect{};

template <typename... CPOs>
using _any_void_sender_of =
  typename _any::_with<CPOs...>::template any_sender_of<>;

template <typename... CPOs>
using any_scheduler_impl =
  any_unique_t<
    _schedule_and_connect<CPOs...>,
    _copy_as<any_scheduler<CPOs...>>,
    _get_type_index,
    overload<bool(const this_&, const any_scheduler<CPOs...>&) noexcept>(_equal_to)>;

template <typename... CPOs>
struct _with<CPOs...>::any_scheduler {
  template (typename Scheduler)
    (requires (!same_as<Scheduler, any_scheduler>) AND scheduler<Scheduler>)
  /* implicit */ any_scheduler(Scheduler sched)
    : impl_((Scheduler&&) sched) {}

  any_scheduler(any_scheduler&&) noexcept = default;
  any_scheduler(const any_scheduler& that)
    : impl_(_copy_as<any_scheduler>(that.impl_).impl_) {}

  any_scheduler& operator=(any_scheduler&&) noexcept = default;
  any_scheduler& operator=(const any_scheduler& that) {
    impl_ = _copy_as<any_scheduler>(that.impl_).impl_;
    return *this;
  }

  struct _sender {
    template <template <class...> class Variant, template <class...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <class...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    _sender(_sender&&) noexcept = default;

    template (typename Receiver)
      (requires receiver_of<Receiver> AND
        (invocable<CPOs, Receiver const&> &&...))
    any_operation_state_for<Receiver> connect(Receiver rec) && {
      any_scheduler_impl<CPOs...> const& impl = sched_.impl_;
      return any_operation_state_for<Receiver>{
          (Receiver&&) rec,
          [&impl](_void_receiver_ref<CPOs...> rec2) {
            return _schedule_and_connect<CPOs...>(
                impl, (_void_receiver_ref<CPOs...>&&) rec2);
          }
        };
    }

  private:
    friend any_scheduler;
    _sender(any_scheduler const* sched)
      : sched_(*sched)
    {}
    // TODO This does a dynamic allocation. Fix this by:
    // 1. Implementing the small-object optimization in any_unique, and
    // 2. Provide hooks so that _sender can take a strong reference on
    //    the impl when it's dynamically allocated.
    any_scheduler sched_;
  };

  _sender schedule() const {
    return _sender{this};
  }

  friend _equal_to_fn;
  friend bool operator==(const any_scheduler& left, const any_scheduler& right) noexcept {
    return _equal_to(left.impl_, right);
  }
  friend bool operator!=(const any_scheduler& left, const any_scheduler& right) noexcept {
    return !(left == right);
  }

private:
  any_scheduler_impl<CPOs...> impl_;
};

template <typename... CPOs>
using any_scheduler_ref_impl = any_ref_t<_schedule_and_connect<CPOs...>>;

template <typename... CPOs>
struct _with<CPOs...>::any_scheduler_ref {
  template (typename Scheduler)
    (requires (!same_as<Scheduler const, any_scheduler_ref const>) AND scheduler<Scheduler>)
  /* implicit */ any_scheduler_ref(Scheduler& sched)
    : impl_(sched) {}

  struct _sender {
    template <template <class...> class Variant, template <class...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <class...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    _sender(_sender&&) noexcept = default;

    template (typename Receiver)
      (requires receiver_of<Receiver> AND
        (invocable<CPOs, Receiver const&> &&...))
    any_operation_state_for<Receiver> connect(Receiver rec) && {
      any_scheduler_ref_impl<CPOs...> const& impl = sched_.impl_;
      return any_operation_state_for<Receiver>{
          (Receiver&&) rec,
          [&impl](_void_receiver_ref<CPOs...> rec2) {
            return _schedule_and_connect<CPOs...>(
                impl, (_void_receiver_ref<CPOs...>&&) rec2);
          }
        };
    }

  private:
    friend any_scheduler_ref;
    _sender(any_scheduler_ref const* sched)
      : sched_(*sched)
    {}
    any_scheduler_ref sched_;
  };

  _sender schedule() const {
    return _sender{this};
  }

  friend bool operator==(const any_scheduler_ref& left, const any_scheduler_ref& right) noexcept {
    return left.impl_ == right.impl_;
  }
  friend bool operator!=(const any_scheduler_ref& left, const any_scheduler_ref& right) noexcept {
    return !(left == right);
  }

private:
  any_scheduler_ref_impl<CPOs...> impl_;
};

} // namespace _any_sched

using any_scheduler = _any_sched::any_scheduler<>;
using any_scheduler_ref = _any_sched::any_scheduler_ref<>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
