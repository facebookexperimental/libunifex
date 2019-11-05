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

#include <unifex/config.hpp>
#include <unifex/overload.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/this.hpp>
#include <unifex/type_traits.hpp>

#include <memory>
#include <utility>

namespace unifex {

namespace detail {

template <typename CPO, typename Sig = typename CPO::type_erased_signature_t>
struct vtable_entry;

template <typename CPO, typename Ret, typename... Args, bool NoExcept>
struct vtable_entry<CPO, Ret(Args...) noexcept(NoExcept)> {
  using fn_t =
      Ret(detail::base_cpo_t<CPO>, replace_this_with_void_ptr_t<Args>...)
      noexcept(NoExcept);

  constexpr fn_t* get() const noexcept {
    return fn_;
  }

  template <typename T>
  static constexpr vtable_entry create() noexcept {
    // auto& f = vtable_entry::concrete_impl<T>;
    // constexpr fn_t* f = &vtable_entry::concrete_impl<T>;
    // return vtable_entry{&f};
    return vtable_entry{[](
          detail::base_cpo_t<CPO> cpo,
          replace_this_with_void_ptr_t<Args>... args) noexcept(NoExcept) {
        void* thisPointer = extract_this<Args...>{}(args...);
        T& obj = *static_cast<T*>(thisPointer);
        return std::move(cpo)(
            replace_this<Args>::get((Args &&) args, obj)...);
      }};
  }

 private:

  explicit constexpr vtable_entry(fn_t* fn) noexcept : fn_(fn) {}

  fn_t* fn_;
};

template <typename... CPOs>
struct vtable : private vtable_entry<CPOs>... {
  template <typename T>
  static constexpr vtable create() noexcept {
    return vtable{vtable_entry<CPOs>::template create<T>()...};
  }

  template <typename CPO>
  constexpr auto get() const noexcept -> typename vtable_entry<CPO>::fn_t* {
    const vtable_entry<CPO>& entry = *this;
    return entry.get();
  }

 private:
  explicit constexpr vtable(vtable_entry<CPOs>... entries) noexcept
      : vtable_entry<CPOs>{entries}... {}
};

template <typename... CPOs>
struct indirect_vtable_holder {
  template <typename T>
  static indirect_vtable_holder create() {
    static constexpr vtable<CPOs...> v = vtable<CPOs...>::template create<T>();
    return indirect_vtable_holder{v};
  }

  const vtable<CPOs...>& operator*() const noexcept {
    return vtable_;
  }

  const vtable<CPOs...>* operator->() const noexcept {
    return std::addressof(vtable_);
  }

 private:
  constexpr indirect_vtable_holder(const vtable<CPOs...>& vtable)
      : vtable_(vtable) {}

  const vtable<CPOs...>& vtable_;
};

template <typename... CPOs>
struct inline_vtable_holder {
  template <typename T>
  static constexpr inline_vtable_holder create() {
    return inline_vtable_holder{vtable<CPOs...>::template create<T>()};
  }

  const vtable<CPOs...>& operator*() const noexcept {
    return vtable_;
  }

  const vtable<CPOs...>* operator->() const noexcept {
    return std::addressof(vtable_);
  }

 private:
  constexpr inline_vtable_holder(const vtable<CPOs...>& vtable)
      : vtable_(vtable) {}

  vtable<CPOs...> vtable_;
};

template <typename... CPOs>
using vtable_holder = std::conditional_t<
    (sizeof...(CPOs) <= 2),
    inline_vtable_holder<CPOs...>,
    indirect_vtable_holder<CPOs...>>;

template <
    typename Derived,
    typename CPO,
    typename Sig = typename CPO::type_erased_signature_t>
struct with_type_erased_tag_invoke;

template <
    typename Derived,
    typename CPO,
    typename Ret,
    typename... Args,
    bool NoExcept>
struct with_type_erased_tag_invoke<
    Derived,
    CPO,
    Ret(Args...) noexcept(NoExcept)> {
  friend Ret tag_invoke(
      base_cpo_t<CPO> cpo,
      replace_this_t<Args, Derived>... args) noexcept(NoExcept) {
    auto& t = extract_this<Args...>{}(args...);
    void* objPtr = t.get_object_address();
    auto* fnPtr = t.get_vtable()->template get<CPO>();
    return fnPtr(
        std::move(cpo),
        replace_this<Args>::get((Args &&) args, objPtr)...);
  }
};

template <
    typename Derived,
    typename CPO,
    typename Sig = typename CPO::type_erased_signature_t>
struct with_forwarding_tag_invoke;

template <
    typename Derived,
    typename CPO,
    typename Ret,
    typename... Args,
    bool NoExcept>
struct with_forwarding_tag_invoke<
    Derived,
    CPO,
    Ret(Args...) noexcept(NoExcept)> {
  friend Ret tag_invoke(
      detail::base_cpo_t<CPO> cpo,
      replace_this_t<Args, Derived>... args) noexcept(NoExcept) {
    auto& wrapper = extract_this<Args...>{}(args...);
    auto& wrapped = wrapper.value;
    return std::move(cpo)(replace_this<Args>::get((Args &&) args, wrapped)...);
  }
};

inline constexpr struct deallocate_cpo {
  using type_erased_signature_t = void(this_&&) noexcept;

  template <
      typename T,
      std::enable_if_t<is_tag_invocable_v<deallocate_cpo, T&&>, int> = 0>
  void operator()(T&& obj) const noexcept {
    tag_invoke(deallocate_cpo{}, (T &&) obj);
  }
} deallocate;

} // namespace detail

template <typename... CPOs>
class any_unique
    : private detail::
          with_type_erased_tag_invoke<any_unique<CPOs...>, CPOs>... {
 public:
  template <typename Concrete, typename Allocator, typename... Args>
  explicit any_unique(
      std::allocator_arg_t,
      Allocator alloc,
      std::in_place_type_t<Concrete>,
      Args&&... args)
      : vtable_(vtable_holder_t::template create<
                concrete_impl<Concrete, Allocator>>()) {
    using concrete_type = concrete_impl<Concrete, Allocator>;
    using allocator_type = typename concrete_type::allocator_type;
    using allocator_traits = std::allocator_traits<allocator_type>;
    allocator_type typedAllocator{std::move(alloc)};
    auto ptr = allocator_traits::allocate(typedAllocator, 1);
    try {
      // TODO: Ideally we'd use allocator_traits::construct() here but
      // that makes it difficult to provide consistent behaviour across
      // std::allocator and std::pmr::polymorphic_allocator as the latter
      // automatically injects the extra allocator_arg/alloc params which
      // ends up duplicating them. But std::allocator doesn't do the same
      // injection of the parameters.
      ::new ((void*)ptr)
          concrete_type{std::allocator_arg, typedAllocator, (Args &&) args...};
    } catch (...) {
      allocator_traits::deallocate(typedAllocator, ptr, 1);
      throw;
    }
    impl_ = static_cast<void*>(ptr);
  }

  template <
      typename Concrete,
      typename Allocator,
      std::enable_if_t<
          !(std::is_same_v<std::allocator_arg_t, std::decay_t<Concrete>> ||
            instance_of_v<std::in_place_type_t, std::decay_t<Concrete>>),
          int> = 0>
  any_unique(Concrete&& concrete, Allocator alloc)
      : any_unique(
            std::allocator_arg,
            std::move(alloc),
            std::in_place_type<std::remove_cvref_t<Concrete>>,
            (Concrete &&) concrete) {}

  template <typename Concrete, typename... Args>
  explicit any_unique(std::in_place_type_t<Concrete> tag, Args&&... args)
      : any_unique(
            std::allocator_arg,
            std::allocator<unsigned char>{},
            tag,
            (Args &&) args...) {}

  template <
      typename Concrete,
      std::enable_if_t<!instance_of_v<std::in_place_type_t, Concrete>, int> = 0>
  any_unique(Concrete&& concrete)
      : any_unique(
            std::in_place_type<std::remove_cvref_t<Concrete>>,
            (Concrete &&) concrete) {}

  any_unique(any_unique&& other) noexcept
      : impl_(std::exchange(other.impl_, nullptr)), vtable_(other.vtable_) {}

  ~any_unique() {
    if (impl_ != nullptr) {
      auto* deallocateFn = vtable_->template get<detail::deallocate_cpo>();
      deallocateFn(detail::deallocate_cpo{}, impl_);
    }
  }

 private:
  using vtable_holder_t =
      detail::vtable_holder<detail::deallocate_cpo, CPOs...>;

  template <typename Concrete, typename Allocator>
  struct concrete_impl final : private detail::with_forwarding_tag_invoke<
                                   concrete_impl<Concrete, Allocator>,
                                   CPOs>... {
    using allocator_type = typename std::allocator_traits<
        Allocator>::template rebind_alloc<concrete_impl>;

    template <typename... Args>
    explicit concrete_impl(
        std::allocator_arg_t,
        allocator_type alloc,
        Args&&... args) noexcept(std::
                                     is_nothrow_move_constructible_v<
                                         allocator_type>&&
                                         std::is_nothrow_constructible_v<
                                             Concrete,
                                             Args...>)
        : value((Args &&) args...), alloc(std::move(alloc)) {}

    friend void tag_invoke(
        detail::deallocate_cpo,
        concrete_impl&& impl) noexcept {
      allocator_type allocCopy = std::move(impl.alloc);
      impl.~concrete_impl();
      std::allocator_traits<allocator_type>::deallocate(
          allocCopy, std::addressof(impl), 1);
    }

    UNIFEX_NO_UNIQUE_ADDRESS Concrete value;
    UNIFEX_NO_UNIQUE_ADDRESS allocator_type alloc;
  };

  template <typename Derived, typename CPO, typename Sig>
  friend struct detail::with_type_erased_tag_invoke;

  const vtable_holder_t& get_vtable() const noexcept {
    return vtable_;
  }

  void* get_object_address() const noexcept {
    return impl_;
  }

  void* impl_;
  vtable_holder_t vtable_;
};

template <auto&... CPOs>
using any_unique_t = any_unique<tag_t<CPOs>...>;

} // namespace unifex
