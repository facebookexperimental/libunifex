# Type Erasure in libunifex

Type erasure is an important tool for managing compile-times by insulating different pieces of code from
each other, and also for implementing polymorphism.

The libunifex library provides several generic type-erasing wrappers built on top of `tag_invoke`-based customisation point objects (CPOs).
These generic wrappers can be parameterised on a variadic list of CPOs that define the operations that concrete types must
support and that the type-erased wrapper itself will expose.

## Categories of type-erasing wrappers

There are several different axes on which different type-erasing wrappers can make different decisions:
* **Ownership** - Does the wrapper own the wrapped concrete object?
  * **Unique Ownership** - The wrapper exclusively owns the lifetime of the concrete object it's wrapping.
  * **Shared Ownership** - The wrapper has shared ownership of the concrete object it's wrapping.
  * **No Ownership** - The lifetime of the wrapped object is managed outside of the wrapper.
* **Object Storage** - How does the wrapper store the wrapped object?
  * **Heap Allocated** - Wrapper stores the concrete object on the heap (possibly allocated using an allocator).
  * **Stored In-Place** - Wrapper stores the concrete object in-place, inline within the storage of the wrapper object.
  * **Hybrid Storage** - Wrapper stores small objects inline in the wrapper and larger objects on the heap. Commonly referred to as small-object optimisation.
  * **External Storage** - Wrapper stores a reference/pointer to the object which is stored in memory not managed by the wrapper.
* **VTable Storage** - How and where is the VTable for the type-erased object stored?
  * **Indirect VTable** - Wrapper stores a pointer to the statically allocated vtable. Pointer indirection to call the method.
  * **In-Place VTable** - Wrapper stores vtable entries in-line inside the wrapper.
    Avoids pointer-indirection at cost of larger wrapper types.
	Also allows casting other type-erased types to wrapper types with a narrower set of supported operations.
  * **Hybrid VTable** - Wrapper stores vtable inline for small vtables and stores it out-of-line for large vtables.
* **Copyability** - Whether the wrapper is copyable, move-only or non-movable.
  * **Copyable** - Wrapper has a copy-constructor and move-constructor (may be either `noexcept(true)` or `noexcept(false)`)
  * **Move-Only** - Wrapper has move-constructor but no copy-constructor (my be either `noexcept(true)` or `noexcept(false)`)
  * **Non-Movable** - Wrapper can only be constructed in-place. Cannot be moved or copied after construction.
* **Comparability** - Whether wrapper type supports equality comparison or not and what its semantics are.
  Note that 
  * **Deep equality comparable** - Wrapper supports `operator==()` with other wrappers of the same type.
    If concrete types are identical then dispatches to `operator==()` for comparing that type, otherwise returns `false`.
  * **Shallow equality comparable** - For wrapper types that do not have unique ownership (e.g. *No Ownership* or *Shared Ownership*)
    we may implement equality comparisons by simply checking whether two instances of the wrapper type refers to the same
    concrete object.
  * **Not comparable** - The wrapper type does not provide equality comparisons.

There are several different type-erasing wrapper types provided by libunifex, each chooses a different point on the design
axes listed above and is usable in different situations depending on desired semantics and performance considerations.

libunifex currently provides the following type-erasing wrappers:
* `unifex::any_unique`
* `unifex::any_ref`
* `unifex::any_object` / `unifex::basic_any_object`

See below for more details on each wrapper type.
Other type-erasing wrapper types may be added in future that choose different points on the design axes listed above.

## General type-erasure design

The type-erasing wrappers are all based around type-erasing objects whose interfaces are defined in terms
of a collection of CPOs, where each CPO is able to be customised by defining overloads of `tag_invoke`
taking the CPO as the first argument.

A type-erasing wrapper has access to the concrete object (either a non-owning reference or owning the object)
and then customises the CPOs on the wrapper by defining `tag_invoke()` overloads that take the wrapper type
and forward the calls through to the corresponding call on the concrete type.

Note that the type-erasing wrappers aren't able to add type-erased member functions. All of the type-erased
operations must be defined as CPOs.

This means that these type-erasing wrappers can generally only be used in conjunction with CPO-based concepts.

### Defining CPO-based interfaces

One of the key components of making these generic type-erasing wrappers work is defining generic
interfaces (aka. concepts) in terms of CPOs.

For example, say we want to define an API for a shape concept.

It might have the following operations:
* `width(const shape& s) -> float`
* `height(const shape& s) -> float`
* `area(const shape& s) -> float`
* `scale_by(const shape& s, float ratio) -> shape`

#### Member-function based concepts

A traditional approach to defining an interface for this would be to require that these operations
are provided as member functions on the object. Thus a `shape` concept might look like this:

```c++
namespace shapes
{
  template<typename T>
  concept shape =
    std::copyable<T> &&
    requires(const T& s, float ratio) {
      { s.width() } -> std::same_as<float>;
      { s.height() } -> std::same_as<float>;
      { s.area() } -> std::same_as<float>;
      { s.scale_by(ratio) } -> std::same_as<T>;
    };
}
```
and a concrete implementation of this concept might look like this:
```c++
namespace shapes
{
  struct square {
    float size;

  public:
    explicit square(float size) : size(size) {}

    float width() const { return size; }
    float height() const { return size; }
    float area() const { return size * size; }
    square scale_by(float ratio) const { return square{size * ratio}; }
  };
}
```
and then to define a type-erasing wrapper around these we would generally have
to define a new class that also defined these methods, specifically for
type-erasing objects of this particular concept.

For example, something like this (details omitted for brevity):
```c++
namespace shapes
{
  class any_shape {
  public:
    any_shape(shape auto s);
    any_shape(const any_shape& s);
    ~any_shape();

    float width() const;
    float height() const;
    float area() const;
    any_shape scale_by(float ratio) const;

  private:
    // Type-erasing implementation details here.
  };
}
```

#### Customisation-Point Object based concepts

A CPO-based interface would instead define these operations as namespace-scope CPOs as follows:
```c++
namespace shapes
{
  inline constexpr struct width_t {
    using type_erased_signature_t = float(const unifex::this_&);

    template<typename T>
      requires unifex::tag_invocable<width_t, const T&>
    float operator()(const T& x) const {
      return unifex::tag_invoke(*this, x);
    }
  } width;

  inline constexpr struct height_t {
    using type_erased_signature_t = float(const unifex::this_&);

    template<typename T>
      requires unifex::tag_invocable<height_t, const T&>
    float operator()(const T& x) const {
      return unifex::tag_invoke(*this, x);
    }
  } height;

  inline constexpr struct area_t {
    using type_erased_signature_t = float(const unifex::this_&);

    template<typename T>
      requires unifex::tag_invocable<area_t, const T&>
    float operator()(const T& x) const {
      return unifex::tag_invoke(*this, x);
    }
  } area;

  inline constexpr struct scale_by_t {
    template<typename T>
      requires unifex::tag_invocable<scale_by_t, const T&, float>
    auto operator()(const T& x, float ratio) const
        -> unifex::tag_invoke_result_t<scale_by_t, const T&, float> {
      return unifex::tag_invoke(*this, x, float(ratio));
    }
  } scale_by;
}
```

Note that some of these CPOs define a `type_erased_signature_t` type-alias
which allows them to be more easily used in type-erased wrappers. We'll cover
more on this later.

With these CPOs defined we can now define the `shape` concept in terms of them:
```c++
namespace shapes
{
  template<typename T>
  concept shape =
    std::copyable<T> &&&
    requires(const T& s, float ratio) {
      shapes::width(s);
      shapes::height(s);
      shapes::area(s);
      { shapes::scale_by(s, ratio) } -> std::same_as<T>;
    };
}
```

And a concrete implementation of a shape class:
```c++
namespace shapes
{
  struct square {
    float size;
  public:
    explicit square(float size) : size(size) {}

    friend float tag_invoke(tag_t<shapes::width>, const square& s) {
      return s.size;
    }

    friend float tag_invoke(tag_t<shapes::height>, const square& s) {
      return s.size;
    }

    friend float tag_invoke(tag_t<shapes::area>, const square& s) {
      return s.size * s.size;
    }

    friend square tag_invoke(tag_t<shapes::scale_by>, const square& s, float ratio) {
      return square{s.size * ratio};
    }
  };
}
```

This is a bit of extra boiler-plate to setup the CPOs and the syntax is
not quite as nice to define customisations of these operations compared
to the member-function approach.

However, one benefit of doing this is that now we have some namespace-scoped
identifiers that can be used to explicitly name these operations without
conflicting with similarly named identifiers used by other libraries.

Then, once we have done this, defining the type-erasing wrapper type is now
a matter of choosing the wrapper with the desired semantics.

For example, say there is an `any_value<CPOs...>` type-erasing wrapper that
supports copyability and a small-object optimisation

And for convenience below, assume there exists the following `any_value_t` alias
so that we don't need to write `tag_t` so often.
```c++
template<auto&... CPOs>
using any_value_t = any_value<unifex::tag_t<CPOs>...>;
```

### Using type-erasing wrappers with CPO-based interfaces 

If we didn't have the need to return an object of the same type (for the `scale_by` method)
then we could define an `any_shape` type-erasing wrapper as a simple type-alias of `any_value`.

For example:
```c++
namespace shapes
{
  using any_shape = unifex::any_value_t<shapes::width,
                                        shapes::height,
                                        shapes::area>;
}
```

Such a type could be used as follows:
```c++
shapes::any_shape s = shapes::square(2.0f);

float width = shapes::width(s);   // -> 2.0f
float height = shapes::height(s); // -> 2.0f
float area =  shapes::area(s);    // -> 4.0f
```

However, this `any_shape` type wouldn't satisfy the `shape` concept we defined above
as it doesn't support the `shapes::scale_by` operation.

Since the `scale_by` doesn't define a default type-erased signature (it doesn't know
what the return-type is in advance) we need some way to specify which overload of
`scale_by` we should be type-erasing.

There is a helper utility called `unifex::overload` that allows you to annotate
a CPO with a particular function signature that you want to type-erase.

Also, we can't just define the `shapes::scale_by` in the same way, since its
type-erased signature needs to reference the type-erased wrapper type itself,
which ends up making the type-definition depend on itself.

Before we dive into trying to add the `shapes::scale_by` CPO to the `any_shape`
type, lets take a little detour into how to define overloads.

#### Specifying Overloads

A generic type-erasing wrapper is typically parameterised by the set of overloads of CPOs
that the type-erasing wrapper is intended to support and forward through to the
concrete implementation.

To customise a particular overload of a CPO it needs to know the signature of the CPO,
including which parameter corresponds to the type-erased object.

Type-erased wrappers will query this signature for a given CPO overload, identified by the type `CPO`,
by looking at the `CPO::type_erased_signature_t` nested type-alias. This will be a function signature
with exactly one parameter having the `unifex::this_` type as a placeholder representing the type-erased
object.

A CPO overload is described using one of two approaches:
* For CPOs that take only one generic argument which is the type-erased wrapper can define
  a default `type_erased_signature_t` type-alias on the CPO itself.
* For CPOs that have additional generic parameters, we can use the `unifex::overload()`
  helper to define a CPO that inherits from the original CPO and adds the
  `type_erased_signature_t` type-alias for the particular overload being
  customised.

For example: A CPO that takes a single parameter and always returns a particular type.
```c++
struct blocking_t {
  using type_erased_signature_t = unifex::blocking_kind(const unifex::this_&) noexcept;

  template<typename T>
    requires unifex::tag_invocable<blocking_t, const T&>
  unifex::blocking_kind operator()(const T& value) const noexcept {
    return unifex::tag_invoke(*this, value);
  }
};

inline constexpr blocking_t blocking{};
```

Then the `blocking` CPO can be used directly as a parameter to the type-erased wrapper.
```c++
using any_blocking = unifex::any_unique_t<blocking>;
```

For example: A CPO that has other parameters that need to be specified can use the
`unifex::overload()` helper.
```c++

using any_receiver_of_int = unifex::any_unique_t<
  unifex::overload<void(unifex::this_&&, int) noexcept>(unifex::set_value),
  >;
```

#### Using specified


If we were to try we'd end up with a compile-error. e.g.
```c++
namespace shapes
{
  using any_shape = unifex::any_value_t<
    shapes::width,
    shapes::height,
    shapes::area,
    unifex::overload<
      any_shape(const unifex::this_, float)>(shapes::scale_by)>; // error: can't reference 'any_shape' in its own definition
}
```

To work around this recursive definition we need to define the `any_shape` type
as a new class that inherits from the appropriate `any_value_t` specialisation.
As the definition of a class can refer to itself the cyclic dependency is no
longer a problem.

e.g.
```c++
namespace shapes
{
  class any_shape : public unifex::any_value_t<
    shapes::width,
    shapes::height,
    shapes::area,
    unifex::overload<any_shape(const unifex::this_&, float)>(shapes::scale_by)>
  {
  private:
    using base_t = unifex::any_value_t<
      shapes::width,
      shapes::height,
      shapes::area,
      unifex::overload<any_shape(const unifex::this_&, float)>(shapes::scale_by)>;
  public:
    // Inherit the constructors
    using base_t::base_t;
  };
}
```

Then once we have this definition we can use the `any_shape` type as above,
but this time it now supports the `shapes::scale_by()` operation.

e.g.
```c++
shapes::any_shape s = shapes::square(2.0f);
shapes::any_shape scaled = shapes::scale_by(s, 5.0f);
float width2 = shapes::width(scaled); // -> 10.0f
float area = shapes::area(s);         // -> 100.0f
```

And so now we have a type-erased wrapper that should itself satisfy the `shape` concept.


# Type-Erasing Wrapper Types

## `unifex::any_unique`

The `any_unique` type-erasing wrapper is modelled loosely after the ownership semantics of the `std::unique_ptr` smart-pointer.

The `any_unique` type is:
* *Unique Ownership* - Wrapped object is owned exclusively by the wrapper.
* *Heap Allocated* - Always stores the wrapped object in a heap-allocation. The allocation may be customised by passing an allocator with `std::allocator_arg` to the constructor.
* *Hybrid VTable* - Stores VTable inline if 2 or less CPOs listed, otherwise out-of-line (max wrapper size of 4 pointers).
* *Nothrow Move-only* - Moves ownership of the pointer. The wrapped object remains un-moved in its heap allocation.
* *Not Comparable* - Doesn't provide `operator==`

```c++
// <unifex/any_unique.hpp>
namespace unifex
{
  template<typename... CPOs>
  class any_unique {
  public:
    // Decay-copies the argument. Uses std::allocator to heap-allocate storage.
    template<typename T>
      requires std::constructible_from<std::remove_cvref_t<T>, T> &&
          !(std::same_as<remove_cvref_t<T>, any_unique> || instance-of-in_place_type_t<T>>)
    /* implicit */ any_unique(T&& x);

    // Constructs T in-place, passing (Args&&)args... to the constructor.
    // Uses std::allocator as the allocator.
    template<typename T, typename... Args>
      requires std::constructible_from<T, Args...>
    explicit any_unique(std::in_place_type_t<T>, Args&&... args);

    // Decay-copies 'obj' into heap-allocated storage allocated using 'alloc'.
    template<typename Allocator, typename T>
      requires std::constructible_from<remove_cvref_t<T>, T>
    explicit any_unique(std::allocator_arg_t, Allocator alloc, T&& obj);

    // Constructs a T in-place, passing (Args&&)args... to the constructor.
    // Uses 'alloc' as the allocator.
    template<typename Allocator, typename T, typename... Args>
      requires std::constructible_from<T, Args...>
    explicit any_unique(std::allocator_arg_t, Allocator alloc, std::in_place_type_t<T>, Args&&... args);

    // Move constructor moves ownership of wrapped object.
    // Leaves 'other' in an invalid state.
    any_unique(any_unique&& other) noexcept;

    // Not copyable
    any_unique(const any_unique&) = delete;

    // Destroys the object owned by *this
    ~any_unique();

    // Move-assignment moves ownership of object owned by 'other' to *this.
    // Leaves 'other' in an invalid state.
    any_unique& operator=(any_unique&& other) noexcept;

    // Swap objects owned by two any_unique.
    void swap(any_unique& other) noexcept;
    
    // Add customisation of std::ranges::swap()
    friend void swap(any_unique& a, any_unique& b) noexcept;

  };

  // Helper alias that allows you to pass CPOs directly instead of having
  // to extract their types with 'tag_t<CPO>'.
  template<auto&... CPOs>
  using any_unique_t = any_unique<tag_t<CPOs>...>;
}
```

## `unifex::any_ref`

The `any_ref` type is the type-erased equivalent of `std::reference_wrapper<T>`.

The `any_ref` type is:
* *No Ownership* - Wrapped object lifetime is externally managed.
* *Externally Allocated* - Wrapped object is allocated in storage external to the wrapper.
* *Hybrid VTable* - VTable is stored inline if it contains 3 or fewer entries, otherwise is stored externally.
  This gives the `any_ref` type a maximum of 4 pointers in size.
* *Copyable* - Wrapper is nothrow copyable and just copies the reference.
* *Shallow Equality Comparable* - Provides equality comparisons which indicate whether the two wrappers
  refer to the same concrete object.

```c++
// <unifex/any_ref.hpp>
namespace unifex
{
  template<typename... CPOs>
  class any_ref {
  public:
    // Construct to a reference to 'concrete' type.
    template<typename T>
      requires (!std::same_as<std::remove_cv_t<T>, any_ref>)
    /* implicit */ any_ref(T& concrete) noexcept;

    // Copy the type-erased reference.
    any_ref(const any_ref&) noexcept;
    any_ref& operator=(const any_ref& other) noexcept;

    // Swap contents of two any_ref objects.
    void swap(any_ref& other) noexcept;

    // Add customisation of std::ranges::swap()
    friend void swap(any_ref& a, any_ref& b) noexcept;

    friend bool operator==(const any_ref& a, const any_ref& b) noexcept;
    friend bool operator!=(const any_ref& a, const any_ref& b) noexcept;
  };

  template<auto&... CPOs>
  using any_ref_t = any_ref<tag_t<CPOs>...>;
}
```

## `unifex::any_object`

The `any_object` type-erasing wrapper is a move-only wrapper that implements a small-object
optimisation that allows storing the wrapped object inline if it is smaller than the inline
buffer, and heap-allocates storage for the object if it does not fit in the inline buffer.

Note that `unifex::any_object` is actually a type-alias for `unifex::basic_any_object` with
some suitable defaults. If you want full control over the options then use `basic_any_object`
directly instead of the `any_object` alias.

```c++
// <unifex/any_object.hpp>
namespace unifex
{
  template<std::size_t InlineSize,
           std::size_t InlineAlignment,
           bool RequireNoexceptMove,
           typename DefaultAllocator,
           typename... CPOs>
  class basic_any_object {
    // True if T fits within the small-object buffer.
    template<typename T>
    bool can_be_stored_inplace_v = ...;

    // True if T satisfies all of the requirements of the type-erased wrapper.
    // i.e. Supports all of the listed CPOs and is nothrow destructible and
    // is nothrow move-constructible if RequireNoexceptMove is true.
    template<typename T>
    bool can_be_type_erased_v = ...;
  public:
    // Construct a type-erasing wrapper that holds an object decay-copied from 'object'.
    // Uses 'DefaultAllocator' to heap-allocate storage if it can't be stored inline.
    template<typename T>
      requires can_be_type_erased_v<remove_cvref_t<T>>
    /*implicit*/ basic_any_object(T&& object)
        noexcept(can_be_stored_inplace_v<T> &&
                 std::is_nothrow_constructible_v<remove_cvref_t<T>, T>);

    // Construct a type-erasing wrapper that holds an object decay-copied from 'object'.
    // Uses 'alloc' to heap-allocate storage if it can't be stored inline.
    template<typename T, typename Allocator>
      requires can_be_type_erased_v<remove_cvref_t<T>>
    explicit basic_any_object(std::allocator_arg_t, Allocator alloc, T&& object)
        noexcept(can_be_stored_inplace_v<T> &&
                 std::is_nothrow_constructible_v<remove_cvref_t<T>, T>);

    // Construct a type-erasing wrapper that holds a T initialised using '(Args&&)args...'.
    // Uses 'DefaultAllocator' to heap-allocate storage if it can't be stored inline.
    template<typename T, typename... Args>
      requires can_be_type_erased_v<T>
    explicit basic_any_object(std::in_place_type_t<T>, Args&&... args)
        noexcept(can_be_stored_inplace_v<T> &&
                 std::is_nothrow_constructible_v<T, Args...>);

    // Construct a type-erasing wrapper that holds a T initialised using '(Args&&)args...'.
    // Uses 'alloc' to heap-allocate storage if it can't be stored inline.
    template<typename T, typename Allocator, typename... Args>
      requires can_be_type_erased_v<T>
    explicit basic_any_object(std::allocator_arg_t, Allocator alloc,
                              std::in_place_type_t<T>, Args&&... args)
        noexcept(can_be_stored_inplace_v<T> &&
                 std::is_nothrow_constructible_v<T, Args...>);

    // Not copyable
    basic_any_object(const basic_any_object&) = delete;

    basic_any_object(basic_any_object&& other) noexcept(RequireNoexceptMove);

    // Destroys the type-erased object.
    ~basic_any_object();
  };

  template<std::size_t InlineSize,
           std::size_t InlineAlignment,
           bool RequireNoexceptMove,
           typename DefaultAllocator,
           auto&... CPOs>
  using basic_any_object_t = basic_any_object<InlineSize,
                                              InlineAlignment,
                                              RequireNoexceptMove,
                                              DefaultAllocator,
                                              unifex::tag_t<CPOs>...>;

  template <typename... CPOs>
  using any_object = basic_any_object<3 * sizeof(void*),
                                      alignof(void*),
                                      true,
                                      std::allocator<std::byte>,
                                      CPOs...>;

  template <auto&... CPOs>
  using any_object_t = any_object<unifex::tag_t<CPOs>...>;
}
```
# Implementation Details

Note: This section describes some of the internal details of the type-erasing wrappers.

## V-Tables

A type-erased wrapper takes the set of CPOs it is parameterised by and defines a constructs a vtable
of function-pointers with an entry for each CPO.

This is implemented by two utility classes, defined in the `unifex::detail` namespace.
* `unifex::detail::vtable_entry<CPO>` - An individual entry of a VTable. Holds a function pointer.
* `unifex::detail::vtable<CPOs...>` - A VTable containing an ordered collection of vtable-entries.

There are also a number of classes called "vtable holders" that implement different storage strategies
for the vtable.

### `vtable_entry`

A `vtable_entry<CPO>` holds a function-pointer that type-erases the concrete implementation
of the overload that `CPO` represents for a given type.

The signature of the function-pointer is derived from the `CPO::type_erased_signature_t`
typedef, which is required to be a function type that contains exactly one argument that
is either a possibly cv-qualified `unifex::this_` type, or is an lvalue-reference or
xvalue-reference to such a type.

For example:
```c++
struct my_cpo {
  using type_erased_signature_t = void(const unifex::this_&, float, bool);
};
```

The parameter with the `unifex::this_` type identifies the parameter that is being
type-erased and so when a `vtable_entry` constructs the function-pointer type, it
prepends the CPO type as the first arg and replaces the `unifex::this_` parameter
with `void*` and this parameter is passed a pointer to the type-erased object
managed by the type-erased wrapper.

When a `vtable_entry` is created for a specific concrete type, the function pointer
will point to a function that casts this `void*` back to a correspondingly-qualified
reference to the concrete object and then invoke the CPO with the `unifex::this_`
parameter replaced with the reference to the concrete object.

For example, a `vtable_entry<my_cpo>` holds a function pointer with the signature
`void(my_cpo, void*, float bool)`, and when the entry is created for a given type, `foo`,
will be the equivalent of:
```c++
void concrete_function_for_foo(my_cpo cpo, void* obj, float arg1, bool arg2) {
  return cpo(*static_cast<const foo*>(obj), std::move(arg1), std::move(arg2));
}
```

The static function `vtable_entry<CPO>::create<T>()` handles constructing a `vtable_entry`
instance initialised with the appropriate function-pointer for a concrete type, `T`.

Generally you don't need to deal with `vtable_entry` type directly, the `vtable<CPOs...>`
class (or more commonly, the vtable-holder classes) handle constructing the vtable entries
for a given type.

### `vtable`

The `vtable<CPOs...>` class is a collection of `vtable_entry` objects, indexed by the `CPO`
type for that entry.

To create an instance of a given `vtable<CPOs...>` for a concrete type, `T`, you call the
`vtable<CPOs...>::create<T>()` static function.

Once you have an instance of the `vtable` you can obtain the function-pointer for the
corresponding CPO by calling `vtable.get<CPO>()`.

### V-Table Holders

When a type-erasing wrapper wants to hold a `vtable` there are different storage strategies
for the storage of the vtable, each with varying tradeoffs:
* `unifex::detail::indirect_vtable_holder<CPOs...>`
  Stores a pointer to a static `vtable` object for each concrete type.
  Reduces storage needed for each concrete vtable and storage in type-erasing wrapper is
  just a single pointer (like most C++ class vtable implementations).
  Cost is that there is an indirection required to lookup the vtable entries.

* `unifex::detail::inline_vtable_holder<CPOs...>`
  Stores the entries of the `vtable` inline within the holder instead of storing
  a pointer to a statically allocated table.
  This avoids the indirection through the vtable pointer but comes at the cost of
  every type-erasing wrapper having to store its own copy of the vtable.
  This can be worth it if the vtable is small, but for large vtables it might
  be more expensive (both in extra storage cost and in code required to initialise
  those members).

One advantage of an `inline_vtable_holder` compared with the `indirect_vtable_holder`
is that it supports the ability to cross-cast or upcast to a vtable with a subset of
the vtable-entries of the source object. This can be used to allow building type-erasing
wrappers that support casting from a wrapper with a wider interface to a wrapper with
a narrower interface, or with the CPOs listed in different orders.

The vtable-holder types all implement the same interface which allows code to work
generically with different vtable storage strategies.

This can be used by type-erasing wrapper types to use a hybrid storage strategy.
e.g. to choose to store small vtables inline while larger vtables are stored using
the indirect strategy.

e.g. A type-erasing wrapper can make a compile-time choice of holder based on the
size of the vtable that needs to be stored.
```c++
// when vtable is 2 or fewer entries, store it inline
using vtable_holder_t = std::conditional_t<
  (sizeof...(CPOs) <= 2),
  unifex::detail::inline_vtable_holder<CPOs...>,
  unifex::detail::indirect_vtable_holder<CPOs...>>;
```

### Using V-Tables

A type-erasing wrapper will define the `vtable` type it is going to use.
This will generally be some combination of user-provided CPOs and CPOs for some
built-in operations (like destruction, or move-construction).

For example:
```c++
template<typename... CPOs>
struct my_type_erasing_wrapper {
private:
  struct destroy_t {
    using type_erased_signature_t = void(unifex::this_&) noexcept;
	template<typename T>
	void operator()(T& obj) noexcept {
	  delete std::addressof(obj);
	}
  };

  using vtable_holder_t = unifex::detail::indirect_vtable_holder<destroy_t, CPOs...>;

public:

  ...
  
private:
  vtable_holder_t vtable_;
  void* data_;
};
```

To create a vtable entry for a given type, we can call the static `create<T>()` method
on the vtable-holder class.

e.g. The constructor of `my_type_erasing_wrapper` might look like:
```c++
template<typename... CPOs>
template<typename T>
my_type_erasing_wrapper<CPOs...>::my_type_erasing_wrapper(T obj)
: vtable_(vtable_holder_t::template create<T>())
, data_(new T(std::move(obj)))
{}
```

To call the type-erased functions from a builtin function (like the destructor)
we can lookup a vtable entry by calling `.get<CPO>()` to the the function pointer
and then invoke it with a `void*` pointer to the type-erased object.
```c++
template<typename... CPOs>
my_type_erasing_wrapper<CPOs...>::~my_type_erasing_wrapper() {
  if (data_ != nullptr) {
    auto* destroyFn = vtable_->template get<destroy_t>();
	destroyFn(data_);
  }
}
```

To be able to hook and customise the other `tag_invoke`-based CPOs, however,
we need to introduce a new `tag_invoke()` overload for each CPO where the
signature of the overload is the same as the `type_erased_signature_t` but
with the use of `unifex::this_` replaced with the type-erasing wrapper type.

There is a helper class `unifex::detail::with_type_erased_tag_invoke` that can
be used as a CRTB base of the type-erasing wrapper type to add the necessary
`tag_invoke` overload that forwards through to the corresponding vtable entry.

To allow this helper class to work, the type-erasing wrapper needs to implement
two hidden-friend functions that allow the helper class to access the vtable
and get the pointer to the type-erased object: `get_vtable()` and `get_object_address()`.

So to extend our hypothetical type-erasing wrapper class to customise the other
CPOs, we can modify it like this:
```c++
template<typename... CPOs>
struct my_type_erasing_wrapper
  : private unifex::with_type_erased_tag_invoke<my_type_erasing_wrapper, CPOs>... {
private:
  struct destroy_t {
    using type_erased_signature_t = void(unifex::this_&) noexcept;
	template<typename T>
	void operator()(T& obj) noexcept {
	  delete std::addressof(obj);
	}
  };

  using vtable_holder_t = unifex::detail::indirect_vtable_holder<destroy_t, CPOs...>;

public:

  ...

private:
  friend const vtable_holder_t& get_vtable(const my_type_erasing_wrapper& self) noexcept {
    return vtable_;
  }

  friend void* get_object_address(const my_type_erasing_wrapper& self) noexcept {
    return data_;
  }

   vtable_holder_t vtable_;
  void* data_;
};
```

Then add in the constructor, destructor and a move-constructor and we have a basic,
functional type-erasing wrapper.

```c++
template<typename... CPOs>
struct my_type_erasing_wrapper
  : private unifex::with_type_erased_tag_invoke<my_type_erasing_wrapper, CPOs>... {
private:
  struct destroy_t {
    using type_erased_signature_t = void(unifex::this_&) noexcept;
	template<typename T>
	void operator()(T& obj) noexcept {
	  delete std::addressof(obj);
	}
  };

  using vtable_holder_t = unifex::detail::indirect_vtable_holder<destroy_t, CPOs...>;

public:

  template<typename T>
  my_type_erasing_wrapper(T obj)
  : vtable_(vtable_holder_t::template create<T>())
  , data_(new T(std::move(obj))
  {}

  my_type_erasing_wrapper(my_type_erasing_wrapper&& x) noexcept
  : vtable_(x.vtable_)
  , data_(std::exchange(x.data_, nullptr))
  {}

  ~my_type_erasing_wrapper() {
    if (data_ != nullptr) {
	  vtable_->template get<destroy_t>()(data_);
	}
  }

private:
  friend const vtable_holder_t& get_vtable(const my_type_erasing_wrapper& self) noexcept {
    return vtable_;
  }

  friend void* get_object_address(const my_type_erasing_wrapper& self) noexcept {
    return data_;
  }

  vtable_holder_t vtable_;
  void* data_;
};
```
