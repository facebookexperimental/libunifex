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
namespace _any_unique {

template <typename CPO, typename Sig = typename CPO::type_erased_signature_t>
struct vtable_entry;

template <typename CPO, typename Ret, typename... Args>
struct vtable_entry<CPO, Ret(Args...)> {
  using fn_t =
      Ret(detail::base_cpo_t<CPO>, replace_this_with_void_ptr_t<Args>...);

  constexpr fn_t* get() const noexcept {
    return fn_;
  }

  template <typename T>
  static constexpr _any_unique::vtable_entry<CPO> create() noexcept {
      constexpr fn_t* f = [](
          detail::base_cpo_t<CPO> cpo,
          replace_this_with_void_ptr_t<Args>... args) {
              void* thisPointer = extract_this<Args...>{}(args...);
              T& obj = *static_cast<T*>(thisPointer);
              return std::move(cpo)(
                  replace_this<Args>::get((Args &&) args, obj)...);
      };
    return _any_unique::vtable_entry<CPO>{f};
  }

 private:
  explicit constexpr vtable_entry(fn_t* fn) noexcept
    : fn_(fn) {}

  fn_t* fn_;
};

template <typename CPO, typename Ret, typename... Args>
struct vtable_entry<CPO, Ret(Args...) noexcept> {
    using fn_t =
        Ret(detail::base_cpo_t<CPO>, replace_this_with_void_ptr_t<Args>...)
        noexcept;

    constexpr fn_t* get() const noexcept {
        return fn_;
    }

    template <typename T>
    static constexpr _any_unique::vtable_entry<CPO> create() noexcept {
        constexpr fn_t* f = [](
            detail::base_cpo_t<CPO> cpo,
            replace_this_with_void_ptr_t<Args>... args) noexcept {
                void* thisPointer = extract_this<Args...>{}(args...);
                T& obj = *static_cast<T*>(thisPointer);
                return std::move(cpo)(
                    replace_this<Args>::get((Args&&)args, obj)...);
        };
        return _any_unique::vtable_entry<CPO>{f};
    }

private:
    explicit constexpr vtable_entry(fn_t* fn) noexcept
        : fn_(fn) {}

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
    bool NoExcept,
    typename Sig>
struct _with_type_erased_tag_invoke;
template <
    typename Derived,
    typename CPO,
    bool NoExcept = false,
    typename Sig = typename CPO::type_erased_signature_t>
using with_type_erased_tag_invoke =
    typename _with_type_erased_tag_invoke<Derived, CPO, NoExcept, Sig>::type;

template <
    typename Derived,
    typename CPO,
    bool NoExcept,
    typename Ret,
    typename... Args>
struct _with_type_erased_tag_invoke<
    Derived,
    CPO,
    NoExcept,
    Ret(Args...)> {
  struct type {
   private:
      template <typename T>
      static void* get_object_address(T&& t) {
        return static_cast<T&&>(t).get_object_address();
      }
      template <typename T>
      static auto  get_vtable(T&& t) {
        return static_cast<T&&>(t).get_vtable();
      }
   public:
    friend Ret tag_invoke(
        detail::base_cpo_t<CPO> cpo,
        replace_this_t<Args, Derived>... args) noexcept(NoExcept) {
      auto& t = extract_this<Args...>{}(args...);
      void* objPtr = get_object_address(t);
      auto* fnPtr = get_vtable(t)->template get<CPO>();
      return fnPtr(
          std::move(cpo),
          replace_this<Args>::get((Args &&) args, objPtr)...);
    }
  };
};

template <
    typename Derived,
    typename CPO,
    typename Ret,
    typename... Args>
struct _with_type_erased_tag_invoke<
    Derived,
    CPO,
    false,
    Ret(Args...) noexcept>
  : _with_type_erased_tag_invoke<Derived, CPO, true, Ret(Args...)> {
};

template <
    typename Derived,
    typename CPO,
    bool NoExcept,
    typename Sig>
struct _with_forwarding_tag_invoke;

template <
    typename Derived,
    typename CPO,
    bool NoExcept = false,
    typename Sig = typename CPO::type_erased_signature_t>
using with_forwarding_tag_invoke =
    typename _with_forwarding_tag_invoke<Derived, CPO, NoExcept, Sig>::type;

template <
    typename Derived,
    typename CPO,
    bool NoExcept,
    typename Ret,
    typename... Args>
struct _with_forwarding_tag_invoke<
    Derived,
    CPO,
    NoExcept,
    Ret(Args...)> {
  struct type {
    friend Ret tag_invoke(
        detail::base_cpo_t<CPO> cpo,
        replace_this_t<Args, Derived>... args) noexcept(NoExcept) {
      auto& wrapper = extract_this<Args...>{}(args...);
      auto& wrapped = wrapper.value;
      return std::move(cpo)(replace_this<Args>::get((Args &&) args, wrapped)...);
    }
  };
};

template <
    typename Derived,
    typename CPO,
    typename Ret,
    typename... Args>
struct _with_forwarding_tag_invoke<
    Derived,
    CPO,
    false,
    Ret(Args...) noexcept>
  : _with_forwarding_tag_invoke<Derived, CPO, true, Ret(Args...)> {
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

template <typename... CPOs>
struct _make {
  class type;
};

template <typename... CPOs>
class _make<CPOs...>::type
  : private with_type_erased_tag_invoke<type, CPOs>... {
 public:
  template <typename Concrete, typename Allocator, typename... Args>
  explicit type(
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
  type(Concrete&& concrete, Allocator alloc)
    : type(
          std::allocator_arg,
          std::move(alloc),
          std::in_place_type<std::remove_cvref_t<Concrete>>,
          (Concrete &&) concrete) {}

  template <typename Concrete, typename... Args>
  explicit type(std::in_place_type_t<Concrete> tag, Args&&... args)
    : type(
          std::allocator_arg,
          std::allocator<unsigned char>{},
          tag,
          (Args &&) args...) {}

  template <
      typename Concrete,
      std::enable_if_t<!instance_of_v<std::in_place_type_t, Concrete>, int> = 0>
  type(Concrete&& concrete)
    : type(
          std::in_place_type<std::remove_cvref_t<Concrete>>,
          (Concrete &&) concrete) {}

  type(type&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr)), vtable_(other.vtable_) {}

  ~type() {
    if (impl_ != nullptr) {
      auto* deallocateFn = vtable_->template get<deallocate_cpo>();
      deallocateFn(deallocate_cpo{}, impl_);
    }
  }

 private:
  using vtable_holder_t = vtable_holder<deallocate_cpo, CPOs...>;

  template <typename Concrete, typename Allocator>
  struct _concrete_impl {
    struct type final
      : private with_forwarding_tag_invoke<type, CPOs>... {
      using allocator_type = typename std::allocator_traits<
          Allocator>::template rebind_alloc<type>;

      template <typename... Args>
      explicit type(std::allocator_arg_t, allocator_type alloc, Args&&... args)
        noexcept(std::is_nothrow_move_constructible_v<allocator_type> &&
            std::is_nothrow_constructible_v<Concrete, Args...>)
        : value((Args &&) args...)
        , alloc(std::move(alloc)) {}

      friend void tag_invoke(
          deallocate_cpo,
          type&& impl) noexcept {
        allocator_type allocCopy = std::move(impl.alloc);
        impl.~type();
        std::allocator_traits<allocator_type>::deallocate(
            allocCopy, std::addressof(impl), 1);
      }

      UNIFEX_NO_UNIQUE_ADDRESS Concrete value;
      UNIFEX_NO_UNIQUE_ADDRESS allocator_type alloc;
    };
  };
  template <typename Concrete, typename Allocator>
  using concrete_impl = typename _concrete_impl<Concrete, Allocator>::type;

  template <typename Derived, typename CPO, bool NoExcept, typename Sig>
  friend struct _with_type_erased_tag_invoke;

  const vtable_holder_t& get_vtable() const noexcept {
    return vtable_;
  }

  void* get_object_address() const noexcept {
    return impl_;
  }

  void* impl_;
  vtable_holder_t vtable_;
};
} // namespace _any_unique

template <typename... CPOs>
using any_unique = typename _any_unique::_make<CPOs...>::type;

template <auto&... CPOs>
using any_unique_t = any_unique<tag_t<CPOs>...>;

} // namespace unifex
