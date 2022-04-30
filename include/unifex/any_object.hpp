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

#include <unifex/scope_guard.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/detail/any_heap_allocated_storage.hpp>
#include <unifex/detail/type_erasure_builtins.hpp>
#include <unifex/detail/vtable.hpp>
#include <unifex/detail/with_abort_tag_invoke.hpp>
#include <unifex/detail/with_forwarding_tag_invoke.hpp>
#include <unifex/detail/with_type_erased_tag_invoke.hpp>

#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  template <
      std::size_t InlineSize,
      std::size_t InlineAlignment,
      bool RequireNoexceptMove,
      typename DefaultAllocator,
      typename... CPOs>
  struct _any_object {
    // WORKAROUND: Define these static-constexpr members here instead of in the
    // nested
    // '::type' class below to work around an issue under MSVC where usages of
    // these variables in requires-clauses in 'type' constructors complains
    // about use of uninitialized variables causing these expressions to not be
    // constexpr.

    // Pad size/alignment out to allow storage of at least a pointer.
    static constexpr std::size_t padded_alignment =
        InlineAlignment < alignof(void*) ? alignof(void*) : InlineAlignment;
    static constexpr std::size_t padded_size =
        InlineSize < sizeof(void*) ? sizeof(void*) : InlineSize;

    // If it doesn't have a nothrow move-constructor and we need it to have a
    // nothrow move-constructor then we fall back to heap-allocating the object
    // and then we just move ownership of the pointer instead of moving the
    // underlying object.
    template <typename T>
    static constexpr bool can_be_stored_inplace_v = (sizeof(T) <= padded_size &&
                                                     alignof(T) <=
                                                         padded_alignment) &&
        (!RequireNoexceptMove || std::is_nothrow_move_constructible_v<T>);

    template <typename T>
    static constexpr bool can_be_type_erased_v = unifex::detail::
        supports_type_erased_cpos_v<T, detail::_destroy_cpo, CPOs...>;

    struct invalid_obj
      : private detail::with_abort_tag_invoke<invalid_obj, CPOs>... {};

    static_assert(can_be_type_erased_v<invalid_obj>);

    class type;
  };

  template <typename T>
  inline constexpr bool _is_in_place_type = false;
  template <typename T>
  inline constexpr bool _is_in_place_type<std::in_place_type_t<T>> = true;

  template <typename T>
  inline constexpr bool _is_any_object_tag_argument =
      _is_in_place_type<T> || std::is_same_v<T, std::allocator_arg_t>;

  template <
      std::size_t InlineSize,
      std::size_t InlineAlignment,
      bool RequireNoexceptMove,
      typename DefaultAllocator,
      typename... CPOs>
  class _any_object<
      InlineSize,
      InlineAlignment,
      RequireNoexceptMove,
      DefaultAllocator,
      CPOs...>::type : private with_type_erased_tag_invoke<type, CPOs>... {
    using vtable_holder_t = detail::indirect_vtable_holder<
        detail::_destroy_cpo,
        detail::_move_construct_cpo<RequireNoexceptMove>,
        CPOs...>;

  public:
    template(typename T)                                        //
        (requires(!same_as<remove_cvref_t<T>, type>) AND        //
         (!_is_any_object_tag_argument<remove_cvref_t<T>>) AND  //
             constructible_from<remove_cvref_t<T>, T> AND                  //
             _any_object::can_be_type_erased_v<remove_cvref_t<T>> AND        //
         (_any_object::can_be_stored_inplace_v<remove_cvref_t<T>> ||         //
          default_constructible<DefaultAllocator>))             //
        /*implicit*/ type(T&& object) noexcept(
            _any_object::can_be_stored_inplace_v<remove_cvref_t<T>>&&
                std::is_nothrow_constructible_v<remove_cvref_t<T>, T>)
      : type(std::in_place_type<remove_cvref_t<T>>, static_cast<T&&>(object)) {}

    template(typename T, typename Allocator)                   //
        (requires _any_object::can_be_type_erased_v<remove_cvref_t<T>> AND  //
             constructible_from<remove_cvref_t<T>, T>)         //
        explicit type(
            std::allocator_arg_t,
            Allocator allocator,
            T&& value) noexcept(_any_object::can_be_stored_inplace_v<remove_cvref_t<T>>&&
                                    std::is_nothrow_constructible_v<
                                        remove_cvref_t<T>,
                                        T>)
      : type(
            std::allocator_arg,
            std::move(allocator),
            std::in_place_type<remove_cvref_t<T>>,
            static_cast<T&&>(value)) {}

    template(typename T, typename... Args)     //
        (requires constructible_from<T, Args...> AND
                  _any_object::can_be_stored_inplace_v<T>)  //
        explicit type(std::in_place_type_t<T>, Args&&... args) noexcept(
            std::is_nothrow_constructible_v<T, Args...>)
      : vtable_(vtable_holder_t::template create<T>()) {
      ::new (static_cast<void*>(&storage_)) T(static_cast<Args&&>(args)...);
    }

    template(typename T, typename... Args)               //
        (requires constructible_from<T, Args...> AND     //
             _any_object::can_be_type_erased_v<T> AND    //
         (!_any_object::can_be_stored_inplace_v<T>) AND  //
             default_constructible<DefaultAllocator>)    //
        explicit type(std::in_place_type_t<T>, Args&&... args)
      : type(
            std::allocator_arg,
            DefaultAllocator(),
            std::in_place_type<T>,
            static_cast<Args&&>(args)...) {}

    template(typename T, typename Allocator, typename... Args)  //
        (requires _any_object::can_be_type_erased_v<T> AND                   //
             _any_object::can_be_stored_inplace_v<T>)                        //
        explicit type(
            std::allocator_arg_t,
            Allocator,
            std::in_place_type_t<T>,
            Args&&... args) noexcept(std::
                                         is_nothrow_constructible_v<T, Args...>)
      : type(std::in_place_type<T>, static_cast<Args&&>(args)...) {}

    template(typename T, typename Alloc, typename... Args)  //
        (requires _any_object::can_be_type_erased_v<T> AND  //
         (!_any_object::can_be_stored_inplace_v<T>))        //
        explicit type(
            std::allocator_arg_t,
            Alloc alloc,
            std::in_place_type_t<T>,
            Args&&... args)
      // COMPILER: This should ideally be delegating to the constructor:
      //
      //   type(std::in_place_type<
      //          detail::any_heap_allocated_storage<T, Alloc, CPOs...>,
      //        std::allocator_arg, std::move(alloc), std::in_place_type<T>,
      //        static_cast<Args&&>(args)...)
      //
      // But doing so causes an infinite recursion of template constructor
      // instantiations under MSVC 14.31.31103 (as well as other versions).
      // So instead we duplicate the body of the constructor this would end
      // up calling here to avoid the recursive instantiations.
      : vtable_(vtable_holder_t::template create<
                detail::any_heap_allocated_storage<T, Alloc, CPOs...>>()) {
      ::new (static_cast<void*>(&storage_))
          detail::any_heap_allocated_storage<T, Alloc, CPOs...>(
              std::allocator_arg,
              std::move(alloc),
              std::in_place_type<T>,
              static_cast<Args&&>(args)...);
    }

    type(const type&) = delete;

    type(type&& other) noexcept(RequireNoexceptMove) : vtable_(other.vtable_) {
      auto* moveConstruct = vtable_->template get<
          detail::_move_construct_cpo<RequireNoexceptMove>>();
      moveConstruct(
          detail::_move_construct_cpo<RequireNoexceptMove>{},
          &storage_,
          &other.storage_);
    }

    ~type() {
      auto* destroy = vtable_->template get<detail::_destroy_cpo>();
      destroy(detail::_destroy_cpo{}, &storage_);
    }

    // Assign from another type-erased instance.
    type& operator=(type&& other) noexcept(RequireNoexceptMove) {
      if (std::addressof(other) != this) {
        auto* destroy = vtable_->template get<detail::_destroy_cpo>();
        destroy(detail::_destroy_cpo{}, &storage_);

        // Assign the vtable for an empty, trivially constructible/destructible
        // object. Just in case the move-construction below throws. So we at
        // least leave the current object in a valid state.
        if constexpr (!RequireNoexceptMove) {
          vtable_ = vtable_holder_t::template create<invalid_obj>();
        }

        auto* moveConstruct = other.vtable_->template get<
            detail::_move_construct_cpo<RequireNoexceptMove>>();
        moveConstruct(
            detail::_move_construct_cpo<RequireNoexceptMove>{},
            &storage_,
            &other.storage_);

        // Now that we've successfully constructed a new object we can overwrite
        // the 'invalid_obj' vtable with the one for the new object.
        vtable_ = other.vtable_;

        // Note that we are leaving the source object in a constructed state
        // rather than unconditionally destroying it here as doing so would mean
        // we'd need to assign the 'invalid_obj' vtable to the source object and
        // then the source obj would incur an extra indirect call to the
        // `invalid_obj` vtable destructor entry.
      }

      return *this;
    }

    template(typename T)                                      //
        (requires _any_object::can_be_type_erased_v<T> AND                 //
             constructible_from<remove_cvref_t<T>, T> AND     //
                 _any_object::can_be_stored_inplace_v<remove_cvref_t<T>>)  //
        type&
        operator=(T&& value) noexcept(
            std::is_nothrow_constructible_v<remove_cvref_t<T>, T>) {
      auto* destroy = vtable_->template get<detail::_destroy_cpo>();
      destroy(detail::_destroy_cpo{}, &storage_);

      using value_type = remove_cvref_t<T>;

      if (!std::is_nothrow_constructible_v<value_type, T>) {
        vtable_ = vtable_holder_t::template create<invalid_obj>();
      }

      ::new (static_cast<void*>(&storage_)) value_type(static_cast<T&&>(value));

      vtable_ = vtable_holder_t::template create<value_type>();

      return *this;
    }

    template(typename T)                                      //
        (requires _any_object::can_be_type_erased_v<T> AND                 //
             constructible_from<remove_cvref_t<T>, T> AND     //
                 default_constructible<DefaultAllocator> AND  //
         (!_any_object::can_be_stored_inplace_v<remove_cvref_t<T>>))       //
        type&
        operator=(T&& value) {
      auto* destroy = vtable_->template get<detail::_destroy_cpo>();
      destroy(detail::_destroy_cpo{}, &storage_);

      vtable_ = vtable_holder_t::template create<invalid_obj>();

      using value_type = detail::any_heap_allocated_storage<
          remove_cvref_t<T>,
          DefaultAllocator,
          CPOs...>;

      ::new (static_cast<void*>(&storage_)) value_type(
          std::allocator_arg,
          DefaultAllocator{},
          std::in_place_type<remove_cvref_t<T>>,
          static_cast<T&&>(value));

      vtable_ = vtable_holder_t::template create<value_type>();

      return *this;
    }

  private:
    friend const vtable_holder_t& get_vtable(const type& self) noexcept {
      return self.vtable_;
    }

    friend void* get_object_address(const type& self) noexcept {
      return const_cast<void*>(static_cast<const void*>(&self.storage_));
    }

    vtable_holder_t vtable_;
    alignas(padded_alignment) std::byte storage_[padded_size];
  };

  template <
      std::size_t InlineSize,
      std::size_t InlineAlignment,
      bool RequireNoexceptMove,
      typename DefaultAllocator,
      typename... CPOs>
  using basic_any_object = typename _any_object<
      InlineSize,
      InlineAlignment,
      RequireNoexceptMove,
      DefaultAllocator,
      CPOs...>::type;

  template <
      std::size_t InlineSize,
      std::size_t InlineAlignment,
      bool RequireNoexceptMove,
      typename DefaultAllocator,
      auto&... CPOs>
  using basic_any_object_t = basic_any_object<
      InlineSize,
      InlineAlignment,
      RequireNoexceptMove,
      DefaultAllocator,
      unifex::tag_t<CPOs>...>;

  // Simpler version that chooses some appropriate defaults for you.
  template <typename... CPOs>
  using any_object = basic_any_object<
      3 * sizeof(void*),
      alignof(void*),
      true,
      std::allocator<std::byte>,
      CPOs...>;

  template <auto&... CPOs>
  using any_object_t = any_object<unifex::tag_t<CPOs>...>;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
