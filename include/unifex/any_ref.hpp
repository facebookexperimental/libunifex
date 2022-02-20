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

#include <unifex/detail/vtable.hpp>
#include <unifex/detail/with_type_erased_tag_invoke.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/swap.hpp>

#include <utility>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

template <typename... CPOs>
struct _any_ref {
  class type;
};

template <typename... CPOs>
class _any_ref<CPOs...>::type
  : private with_type_erased_tag_invoke<type, CPOs>... {

  // Inline up to 3 vtable-entries. Making a max size of 4 pointers.
  using vtable_holder_t = std::conditional_t<
    (sizeof...(CPOs) <= 3),
    detail::inline_vtable_holder<CPOs...>,
    detail::indirect_vtable_holder<CPOs...>>;

 public:
  template (typename Concrete)
    (requires (!same_as<Concrete const, type const>))
  /*implicit*/ type(Concrete& impl) noexcept
    : vtable_(vtable_holder_t::template create<Concrete>())
    , impl_((void*) std::addressof(impl)) {}

  void swap(type& other) noexcept {
    unifex::swap(vtable_, other.vtable_);
    unifex::swap(impl_, other.impl_);
  }

  // Two any_ref's compare equal IFF they refer to the same object (shallow).
  //
  // TODO: This is not entirely accurate as it might be possible to have two
  // any_ref objects constructed from the same object at the same address but
  // with different static-types (e.g. one any_ref is constructed from a base-class)
  // and thus they would have different vtables. However, we can't just compare
  // vtables since it's not guaranteed that function pointers will point to the
  // same address.
  friend bool operator==(type const& left, type const& right) noexcept {
    return left.impl_ == right.impl_;
  }

  friend bool operator!=(type const& left, type const& right) noexcept {
    return !(left == right);
  }

 private:
  friend void swap(type& left, type& right) noexcept {
    left.swap(right);
  }

  friend const vtable_holder_t& get_vtable(const type& self) noexcept {
    return self.vtable_;
  }

  friend void* get_object_address(const type& self) noexcept {
    return self.impl_;
  }

  vtable_holder_t vtable_;
  void* impl_;
};

// any_ref is a type-erased wrapper that has reference-semantics.
//
// It holds a type-erased reference to a concrete object and allows you to invoke CPOs on that
// object 
//
// Copying an any_ref just copies the reference, not the underlying object.
template<typename... CPOs>
using any_ref = typename _any_ref<CPOs...>::type;

template<auto&... CPOs>
using any_ref_t = any_ref<tag_t<CPOs>...>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
