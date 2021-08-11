# Overview

The 'unifex' project is an implementation of the C++ sender/receiver
async programming model.

A sender represents a reification of an operation that delivers its
result by calling another function rather than by returning a value.
This is analogous to how a function-object represents a reification of
a synchronous function that produces its result by returning a value.

The reification of asynchronous operations allows us to compose these
operations using higher-order algorithms, and lazily execute them.

The sender/receiver design defines the interface that asynchronous
operations must implement to allow generic code to compose these
operations together.

The unifex library contains implementations of various asynchronous
operations and algorithms along with some examples that show how
they can be composed.

These include:
* Schedulers
* Timers
* Asynchronous I/O (Linux w/ io_uring)
* Algorithms that encapsulate certain concurrency patterns
* Async streams
* Cancellation
* Coroutine integration

# Design Goals

## Allow encapsulating common concurrency patterns in reusable algorithms

One of the primary goals of the sender/receiver design is to allow applications to
compose asynchronous operations using generic asynchronous algorithms that can be
reused across different domains with arbitrary types of asynchronous operations.

There are a large number of common patterns that are used when writing asynchronous
and concurrent programs.

For example, an application will often want to execute two or more operations concurrently
and wait for them all to complete before continuing, producing a tuple of the results.
However, if one of them fails with an error then any operations that have not yet
completed should be cancelled and then once they have all finished executing, the error
should be propagated.

Implementing such a pattern correctly and efficiently can be difficult and
often requires knowledge of thread synchronisation techniques such as atomics and
memory orders.

Further, implementing this in an ad-hoc way where the details of the
concurrency pattern are interleaved with the logic of surrounding application
code can both obscure the application logic, which is now mixed in with the
synchronization logic necessary to implement this pattern, and increase the
likelihood of potentially subtle bugs in the implementation.

Ideally we would be able to encapsulate these kinds of patterns in a reusable algorithm
that can be given a name, can be well-tested and that can be reused in different situations
with different kinds of asynchronous operations rather than having to implement these
patterns in an ad-hoc fashion in each place they occur.

Encapsulation allows applications to program at a higher-level of abstraction and be less
likely to contain subtle bugs due to the use of well-tested reusable algorithms.

An analogy can be drawn between a library of generic, reusable asynchronous/concurrency
patterns and the standard library algorithms for operating on ranges of values.
By using library abstractions, programs are generally simpler and are more likely to be correct and efficient.

## Define a set of common concepts for asynchrony and execution contexts

To be able to support building generic algorithms over async operations we need
to define the concept representing an async operation.

The sender/receiver design aims to define a common interface for async operations
that supports building generic algorithms that interact with those operations.

This serves a similar role to the `std::range` concept which allows algorithms
to operate generically on different types that represent sequences of values.

## Efficient and natural integration with coroutines

Asynchronous operations should ideally just be able to be `co_await`ed from a coroutine
without every async operation having to explicitly implement an `operator co_await()`.

Awaitable types in coroutines have the ability to avoid heap-allocations needed to
store the state of an asynchronous operation while it is executing by having the
`operator co_await()` method return a temporary `awaiter` object that will live in the
coroutine frame. The async operation can then define data-members in this `awaiter`
object to reserve storage

As the lifetime of the temporary object necessarily spans a suspend-point (the
`await_ready()` method is called prior to suspending and the `await_resume()`
method is called after the coroutine is resumed) this object is guaranteed
to be kept alive for the duration of the operation by the compiler.

Note that in the coroutine/awaitable model, it is the consumer that owns
the storage for the asynchronous operation's state, rather than the producer
owning the consumer's callback. The consumer is responsible for destroying
that state after the operation completes.

With coroutines this naturally occurs because after the coroutine resumes
execution will eventually reach the semicolon and the compiler will generate
a call to the destructor.

The same principle can be applied to callback-based asychronous operations,
where the producer returns an object to the consumer that the consumer needs
to keep alive until the operation completes.

Doing this allows us to write a generic `operator co_await()` that stores
this object on the coroutine frame and thus does not require a separate
heap allocation for that state.

## First-class support for cancellation

Whenever we introduce asynchrony, we also need to deal with concurrency.
By their nature, asynchronous APIs imply some kind of concurrency.

Whenever we are dealing with multiple concurrent operations an application
will inevitably come to the situation where one of those operations completes
and this satisfies some higher-level goal of the application such that we no
longer need the result of the other operations.

To avoid wasting resources in continuing to produce results that are no longer
needed we need to be able to notify those operations that they should stop
executing promptly.

If those operations respond to the request to stop by stopping execution early,
then the operation needs some way to indicate that it stopped early because it
was asked to, and so may not have satisfied its post-conditions.

For cancellation of high-level operations, like downloading a file, to be able
to propagate through to cancel low-level operations, like reading from a socket
or waiting for a timer, cancellation needs to be an intrinsic part of the
asynchronous model.

There needs to be a uniform way for higher-level operations to request a
lower-level operation to stop so that generic algorithms can compose child
operations into concurrency patterns that involve cancellation.

In cases where cancellation is not required we want to make sure
that there is no runtime overhead incurred compared to code that does not have
support for cancellation.

## Support acceleration of algorithms when run on particular execution contexts

Design generic algorithms in such a way that those algorithms can dispatch to more
efficient implementations when passed particular types of arguments.

This is especially important for high-performance-computing use-cases where they
want a particular algorithm, when passed a GPU execution resource, to dispatch
to code that runs accelerated on the GPU, rather than to some generic implementation.

For example, a generic `sequence(op1, op2)` algorithm that accepts two async operations
and returns a composed async operation that performs these two operations in sequence
might want to be customised if passed two gpu-operations to perform gpu-side chaining
of those operations, rather than having to dispatch back to the CPU between each
operation.

Thus algorithms need to be customisation-points if they want to allow user-defined types
to provide more efficient implementations, even when used in a generic context. However, we don't want to put the burden on the implementer of user-defined types
to have to define an implementation of every algorithm, so we still want most
algorithms to have default implementations that are defined in terms of some
set of basis operations - typically the basis operations that comprise the concept,
so that the algorithms are at least functional.

# Differences between this prototype and P2300R1

This design has a number of key differences from the sender/receiver
design described in the paper "`std::execution`"
([P2300r1](https://wg21.link/P2300r1)).

## Namespace `unifex`

All the entities in libunifex are defined in the `::unifex` namespace
or a subnamespace thereof. P2300 proposes to put its facilities in a
new `::std::execution` namespace.

## No eager execution

All of the algorithms in libunifex are strictly lazy with the exception
of `unifex::submit` and `unifex::execute`. P2300 proposes eager flavors
of all its algorithms.

## No completion schedulers

Senders in P2300 optionally report the schedulers the different signals
(value, error, and done) complete on with the `execution::get_completion_scheduler`
query. Libunifex does not implement that query yet, but the plan is to
do so.

## Different sets of algorithms

P2300 proposes a few algorithms that aren't present yet in libunifex
in addition to the eager flavors. These algorithms are yet to be
implemented:

| P2300 algorithm | Libunifex equivalent |
|-----------------|----------------------|
| `execution::transfer` | Not yet implemented |
| `execution::transfer_just` | Not yet implemented |
| `execution::upon_*` | Not yet implemented |
| `execution::into_variant` | Not yet implemented |
| `execution::bulk` | Not implemented (but see `unifex::bulk_schedule`) |
| `execution::split` | Not yet implemented |
| `execution::when_all` | Not yet implemented |
| `execution::when_all_with_variant` | `unifex::when_all` |
| `execution::transfer_when_all` | Not yet implemented |
| `execution::start_detached` | Not yet implemented (but see `unifex::submit`) |

# Outstanding Challenges

There are a number of outstanding challenges that do not yet have complete solutions.
Future iterations and evolution of Unifex will attempt to address these.

## Async cleanup

The Async Stream design in Unifex has support for asynchronous cleanup of the
stream by calling the `cleanup()` customisation-point. This allows a stream to
perform async operations necessary to release its resources, such as joining

We have not yet incorporated the ability to support async cleanup into
Senders and this currently makes it difficult to build certain kinds of
algorithms / concurrency patterns.

See the paper "Adding async RAII to coroutines" ([P1662R0](https://wg21.link/P1662R0))
for more details on the need for async RAII in coroutines.

The same use-cases are required for sender-based asynchronous operations
and whatever solution we end up with for coroutines will need to integrate
with whatever we come up with for sender/receiver.

As a special case, `unifex:task<>` gives coroutines something akin to async
cleanup. The `unifex::at_coroutine_exit` function permits a coroutine to
schedule an asynchronous operation to run automatically when a coroutine
finishes.

## Functionality parity between sender/receiver and coroutines/awaitables

There are some features that sender/receiver supports that coroutines do
not yet support, and there are some features that coroutines supports that
sender/receiver does not yet support.

Ideally, these capabilities would converge in the long-run to the union
of the capabilities of sender/receiver and coroutines.

### Tail Recursion

The main feature that coroutines support that is not currently expressible
using sender/receiver and callbacks in general is support for guaranteed
tail-recursion.

One coroutine can symmetrically transfer execution to another coroutine
without consuming additional stack-space.

This allows coroutines to recursivelly call each other inline without
needing to bounce execution off an executor or trampoline to ensure that
we don't run into a stack-overflow situation.

This is something that the sender/receiver design does not currently
support. If an operation completes synchronously and calls `set_value()`
inline inside the `start()` call then this can potentially mean that
the continuation is executing in a context where additional stack-frames
have been allocated.

If this happens recursively, eg. because we are processing a stream
of synchronously-produced values, then this can result in exhaustion
of available stack-space.

Ideally we could build sender/receiver objects that are also able to
perform tail-calls.

This may require new language feature, such as the `tail return`
keyword proposed in "Core Coroutines" ([P1063](https://wg21.link/P1063)),
or may require explicitly incorporating something like trampolining
into the sender/receiver design.

### Heterogeneous Results

One of the capabilities that sender/receiver supports is being able
to produce different types of results by calling different overloads
of `set_value()` or `set_error()` and passing different types.

This will statically-dispatch the call to the overload of
`set_value()` on the receiver that can handle that result type.

This allows producing a result that could be one of several different
types without needing to encapsulate that result in a type-erasing container;
either a bounded type-erasing container like a `std::variant`, or
an unbounded type-erasing container like a `std::unique_ptr<BaseClass>`.

This can be useful in cases such as a tokeniser where the sender might
produce one of several different token types depending on what characters
were read in from the stream. This allows the tokeniser to invoke the
receiver with the concrete type and avoid needing to type-erase the token.

However, coroutines currently have the limitation that the return-type
of a `co_await` expression can only be a single type (it is deduced from
the return-type of the `await_ready()` method).

This forces the result-types to be coalesced into a single type, which
adds overhead to the result.

Ideally, a coroutine would be able to handle receiving one of multiple
possible types as its results to avoid the need to wrap up the result
in a type-erasing container.

One possible strawman syntax for this might be to introduce a `template co_await`
construct that causes the continuation of the `co_await` expression
to be instantiated for each result-type. e.g.
```c++
template<typename T>
concept token = ...;

struct float_token { ... };
struct string_token { ... };
struct identifier_token { ... };
// etc.

auto parse_token(stream& s) [->] task<token>;

task parse(stream& s) {
    token auto t = template co_await parse_token(s);
    // t now has the concrete token type here
}
```

This could also be incorporated into the `for co_await` loop to allow
iterating over heterogeneous sequences, and would also integrate nicely
with pattern matching:
```c++
template for co_await(token auto&& t : token_stream()) {
  inspect (t) {
    <float_token> ft: ...;
    <string_token> st: ...;
    <identifier_token> it: ...;
    //etc.
  }
}
```

### Multiple Results

Sender/receiver also supports producing multiple values as the result
of an operation. A sender can invoke the `set_value()` call with zero
or more value arguments, allowing it to return multiple values without
needing to pack them into a struct or tuple.

Awaitables currently only support returning either no result, ie. `void`,
or a single value from a `co_await` expression. This forces coroutines to
have to encapsulate multiple results in the

One idea might be to introduce a `co_await...` syntax that allows a
`co_await` expression to complete with a pack of values rather than a
single type.

```c++
task<A, B, C> foo();

auto [a, b, c] = co_await... foo();
```

This can also potentially help unify the `void` and non-`void` cases.

e.g. When implementing a `then()` operation, instead of needing to write this:
```c++
template<typename AsyncOp, typename Func>
task then(AsyncOp op, Func f) {
    if constexpr (std::is_void_v<await_result_t<AsyncOp>>) {
        co_await std::move(op);
        f();
    } else {
        f(co_await std::move(op));
    }
}
```

We could just write:
```c++
template<typename AsyncOp, typename Func>
task then(AsyncOp op, Func f) {
  f(co_await... std::move(op));
}
```

This could optionally be combined with the above `template co_await`
syntax to provide the ability to handle both multiple arguments
and heterogeneous types.

```c++
auto [...results] = template co_await... foo();
// use results...
```

Which would be the equivalent of the following receiver being passed to `foo()`.

```c++
class some_receiver {
  ...

  template<typename... Args>
  friend void tag_invoke(tag_t<set_value>, some_receiver&& r, Args&&... results) {
    // use results...
  }
};
```
