# Customisation point mechanism

This library makes use of a mechanism called `tag_invoke()` that allows
types to customise operations by defining an overload of `tag_invoke()`
that can be found by ADL that takes the customisation-point-object as
the first parameter followed by the rest of the parameters passed to
`CPO::operator()`.

See [P1895R0](https://wg21.link/P1895R0) for more details on `tag_invoke`.

Note that `unifex::tag_invoke()` is itself a customisation-point-object
that forwards on to call the overload of `tag_invoke()` that is found by ADL.
This handles the necessary mechanics needed to construct a
[Niebloid](http://eel.is/c++draft/algorithms.requirements#2)
so that you don't have to do this for every CPO.

Customisations of CPOs using overloads of the `tag_invoke()` function typically
define these overloads as friend-functions to limit the overload set that the
compiler needs to consider when resolving a call to `tag_invoke()`.

For example: Defining a new CPO
```c++
inline struct example_cpo {
  // An optional default implementation
  template<typename T>
  friend bool tag_invoke(example_cpo, const T& x) noexcept {
    return false;
  }

  template<typename T>
  bool operator()(const T& x) const
    noexcept(is_nothrow_tag_invocable_v<example_cpo, const T&>)
    -> tag_invoke_result_t<example_cpo, const T&> {
    // Dispatch to the call to tag_invoke() passing the CPO as the
    // first parameter.
    return tag_invoke(example_cpo{}, x);
  }
} example;
```

Example: Customising a CPO
```c++
struct my_type {
  friend bool tag_invoke(tag_t<example>, const my_type& t) noexcept {
    return t.isExample;
  }

  bool isExample;
}
```

Example: Calling a CPO
```c++
struct other_type {};

void usage() {
  // Customised for this type so will dispatch to custom implementation.
  my_type t{true};
  assert(example(t) == true);

  // Not customised so falls back to default implementation.
  other_type o;
  assert(example(t) == false);
}
```

This also allows wrapper types to forward through subsets of CPOs to the
wrapped object.
```c++
template<typename T, typename Allocator>
struct allocator_wrapper {
  // Customise one CPO.
  friend void tag_invoke(tag_t<get_allocator>, const allocator_wrapper& a) {
    return a.alloc_;
  }

  // Pass through the rest.
  template<typename CPO, typename... Args>
  friend auto tag_invoke(CPO cpo, const allocator_wrapper& x, Args&&... args)
    noexcept(std::is_nothrow_invocable_v<CPO, const T&, Args...>)
    -> std::invoke_result_t<CPO, const T&, Args...> {
    return std::move(cpo)(x.inner_, (Args&&)...);
  }

  Allocator alloc_;
  T inner_;
};
```
