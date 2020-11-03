# Concept Overview

There are a number of key concepts used within this project.

Async operation concepts:
* **Receiver** - A generalisation of a callback that receives the result of some asynchronous operation.
* **Sender** - Represents an operation that delivers its result by invoking one of the receiver's methods.
* **TypedSender** - A sender that describes the types that it will send.
* **OperationState** - An object that holds the state of an asynchronous operation.
* **ManySender** - A sender that sends multiple values to a receiver.
* **AsyncStream** - Like an input range where each element of the sequence
  is lazily produced asynchronously and only when requested.
* **Scheduler** - An object that supports the ability to schedule work onto some other context.
* **TimeScheduler** - A Scheduler that also supports the ability to schedule work to occur at a particular point in time.
* **StopToken** - A concept for different kinds of stop-token, used to signal a request to stop an operation.

## Important Note on C++20 Concepts

The implementation of unifex conditionally uses C++20 concepts if the compiler
supports them.

The C++20 concept definitions listed below are defined as concepts in the unifex
source on compilers that support concepts, or as Boolean variable templates otherwise.

Function templates are constrained either with `requires` clauses or with
`std::enable_if_t` with the help of a set of portability macros.

# Sender/Receiver

A **Sender** represents an operation whose result is delivered
to a **Receiver** object via a call to one of the three customisation-points,
`set_value`, `set_done` or `set_error`, rather than via a return-value.

A `Sender` is a reification of an asynchronous operation, much like a
function-object or lambda is a reification of a synchronous operation.

By reifying an asynchronous operation in with a standard interface
for launching them and providing a continuation we allow them to be
lazily started and composed using generic algorithms.

## Starting an async operation

Note that a `Sender` might represent either a lazily-started operation or
may represent an operation that has already been started (i.e. an "eager" operation).
However, from the perspective of the `Sender` concept we treat
the operation as if it were lazy and needs to be explicitly started.

To initiate an asynchronous operation, you must first `connect()` a
`Sender` and `Receiver` to produce an `OperationState` object. This object
holds the state and encapsulates the logic necessary to execute the
asynchronous operation. A given asynchronous operation may be comprised of
many different steps that need to be executed in order to produce the
operation result and the `OperationState` object represents the
state-machine for that operation.

The operation/state-machine is "started" from the perspective of the
concepts by calling the `start()` customisation-point, passing an lvalue
reference to the operation-state object. Once started, the operation
proceeds until it completes.

This separation of the launch of the operation into `connect()` and
`start()` allows the caller to control placement and lifetime of the
operation-state object. And by virtue of the operation-state being
represented as a type, the caller can also statically know the size of the
operation state and so can place it on the stack, in a coroutine frame or
store it as a member of a class.

## Object-state lifetime

It is the responsibility of the caller of `start()` to ensure that once
`start()` is called that the operation-state object is kept alive until
the operation completes.

Once one of the completion-signalling functions is called on the receiver,
the receiver is permitted to and is responsible for ensuring that the
operation-state object is destroyed.

This means that, from the perspective of the operation-state object,
once the receiver method that signals completion of the operation is
invoked, the caller cannot assume that the operation-state object is
valid as the receiver may have already destroyed it.

Operation state objects are not movable or copyable. You need to construct
the operation state objects in-place, typically relying on copy-elision
to initialise the object in-place as the return-value from `connect()`.

## Completion of an async operation

Completion of the operation is signalled by a successful call to one of
the `set_value()`, `set_done()` or `set_error()` customisation-points
where the first argument is the receiver (or a receiver move-constructed
from it) that was passed to the call to `connect()` that constructed the
operation-state object.

A call to `set_value` indicates that the operation "succeeded"
(ie. its post conditions were fulfilled) and may be invoked with
zero or more additional parameters containing the value(s) that
the operation produced.

A call to `set_error` indicates that the operation "failed"
(ie. its post conditions could not be satisfied).

A call to `set_done` indicates that the operation completed
with neither a value (indicating success) or an error
(indicating failure).
You can think of this as the "none" or "empty" result.

A "none" result is typically produced because a *higher-level goal*
has been achieved and the operation was requested to complete early.
In this case it's not necessarily that the "post conditions couldn't
be satisfied" (they may well have been able to be satisfied if the
operation were allowed to run to completion), but rather that the attempt
to satisfy the post-conditions was aborted early for some higher-level
reason.

For more details on this see the paper "Cancellation is serendipitous-success"
([P1677R2](https://wg21.link/P1677R2)).

Note that there is a distinction between producing a "success"
result with no values (indicated by `set_value(receiver)`) and an "empty"
result (indicated by `set_done(receiver)`). The former would be called
when the operation satisfied its post-conditions, the latter may be
called when the post-conditions have not been satisfied.

## Receiver Concept

A receiver is a generalisation of a callback that receives the result of
an asynchronous operation via a call to one of the three receiver CPOs.

A receiver can also be thought of as a **continuation** of an asynchronous
operation.

Note that there is no single `receiver` concept but rather separate concepts
that relate to whether the receiver is able to receive particular completion
signals.

The `value_receiver<Values...>` concept indicates that an object can receive
a `set_value()` completion signal that is invoked with arguments of type
`Values...`.

The `error_receiver<Error>` concept indicates that an object can receive
a `set_error()` completion signal that is invoked with an error value of
type `Error`.

The `done_receiver` concept indicates that an object can receive a
`set_done()` completion signal.

All receivers are required to be move-constructible and destructible.

These can be described as:
```c++
namespace unifex
{
  // CPOs
  inline constexpr unspecified set_value = unspecified;
  inline constexpr unspecified set_error = unspecified;
  inline constexpr unspecified set_done = unspecified;

  template<typename R>
  concept __receiver_common =
    std::move_constructible<R> &&
    std::destructible<R>;

  template<typename R>
  concept done_receiver =
    __receiver_common<R> &&
    requires(R&& r) {
        set_done((R&&)r);
    };

  template<typename R, typename... Values>
  concept value_receiver =
    __receiver_common<R> &&
    requires(R&& r, Values&&... values) {
      set_value((R&&)r, (Values&&)values...);
    };

  template<typename R, typename Error>
  concept error_receiver =
    __receiver_common<R> &&
    requires(R&& r, Error&& error) {
      set_error((R&&)r, (Error&&)error);
    };
}
```

Different **Sender** types can have a different set of completion
signals that they can potentially complete with and so will typically
have different requirements on the receivers passed to their `connect()`
method.

The above concepts can be composed to constrain the `connect()` operation
for a particular sender to support the set of completion signals that
the sender supports.

### Context Information

Note that receivers are also used to propagate contextual information from the
caller to the callee.

A receiver may customise additional getter CPOs that allow the sender to query for
information about the calling context. For example, to retrieve the StopToken,
Allocator or Scheduler for the enclosing context.

For example: The `get_stop_token()` CPO can be called with a receiver to ask the
receiver for the stop-token to use for this operation. The receiver can communicate
a request for the operation to stop via this stop-token.

NOTE: The set of things that could be passed down as implicit context from caller
to callee via the receiver is an open-set. Applications can extend this set with
additional application-specific contextual information that can be passed through
via the receiver.

TODO: Link to `cancellation.md` file.

## Sender Concept

A **Sender** represents an asynchronous operation that produces its result, signalling
completion, by calling one of the three completion operations on a receiver:
`set_value`, `set_error` or `set_done`.

There is currently no general `sender` base concept.

In general it's not possible to determine whether an object is a sender in isolation
of a receiver. Once you have both a sender and a receiver you can check if a sender
can send its results to a receiver of that type by checking the `sender_to` concept.

This simply checks that you can `connect()` a sender of type `S` to a receiver of
type `R`.

```c++
namespace unifex
{
  // Sender CPOs
  inline constexpr unspecified connect = unspecified;

  // Test whether a given sender and receiver can been connected.
  template<typename S, typename R>
  concept sender_to =
    requires(S&& sender, R&& receiver) {
      connect((S&&)sender, (R&&)receiver);
    };
}
```

TODO: Consider adding some kind of `sender_traits` class or an `is_sender<T>` CPO
that can be specialised to allow a type to opt-in to being classified as a sender
independently of a concrete receiver type.

## TypedSender Concept

A **TypedSender** extends the interface of a **Sender** to support two additional
nested template type-aliases that can be used to query the overloads of
`set_value()` and `set_error()` that it may call on the **Receiver** passed to it.

A nested template type alias `value_types` is defined, which takes two template
template parameters, a `Variant` and a `Tuple`, from which the type-alias produces
a type that is an instantiation of `Variant`, with a template argument for each
overload of `set_value` that may be called, with each template argument being an
instantiation of `Tuple<...>` with a template argument for each parameter that
will be passed to `set_value` after the receiver parameter.

A nested template type alias `error_types` is defined, which takes a single
template template parameter, a `Variant`, from which the type-alias produces
a type that is an instantiation of `Variant`, with a template argument for each
overload of `set_error` that may be called, with each template argument being
the type of the error argument for the call to `set_error`.

A nested `static constexpr bool sends_done` is defined, which indicates whether
the sender might complete with `set_done`.

For example:
```c++
struct some_typed_sender {
 template<template<typename...> class Variant,
          template<typename...> class Tuple>
 using value_types = Variant<Tuple<int>,
                             Tuple<std::string, int>,
                             Tuple<>>;

 template<template<typename...> class Variant>
 using error_types = Variant<std::exception_ptr>;

 static constexpr bool sends_done = true;
 ...
};
```

This `TypedSender` indicates that it may call the following overloads on
the receiver:
- `set_value(R&&, int)`
- `set_value(R&&, std::string, int)`
- `set_value(R&&)`
- `set_error(R&&, std::exception_ptr)`
- `set_done(R&&)`

When querying the `value_types/error_types/sends_done` properties of
a sender you should look them up in the `sender_traits<Sender>` class rather
than on the sender type directly.

e.g. `typename unifex::sender_traits<Sender>::template value_types<std::variant, std::tuple>`

## OperationState Concept

An **OperationState** object contains the state for an individual asynchronous
operation.

The operation-state object is returned by a call to `connect()` that connects
a compatible **Sender** and **Receiver**.

An operation-state object is not movable or copyable.

There are only two things you can do with an operation-state object:
`start()` the operation or destroy the operation-state.

It is valid to destroy an operation-state object only if it hasn't yet been
started or if it has been started and the operation has completed.

```c++
namespace unifex
{
  // CPO for starting an async operation
  inline constexpr unspecified start = unspecified;

  // CPO for an operation-state object.
  template<typename T>
  concept operation_state =
    std::destructible<T> &&
    requires(T& operation) {
      start(operation);
    };
}
```

# ManySender/ManyReceiver

A **ManySender** represents an operation that asynchronously produces zero or
more values, produced by a call to `set_next()` for each value, terminated
by a call to either `set_value()`, `set_done()` or `set_error()`.

This is a general concept that encapsulates both sequences of values (where the calls to
`set_next()` are non-overlapping) and parallel/bulk operations (where there may
be concurrent/overlapping calls to `set_next()` on different threads
and/or SIMD lanes).

A **ManySender** does not have a back-pressure mechanism. Once started, the delivery
of values to the receiver is entirely driven by the sender. The receiver can request
the sender to stop sending values, e.g. by causing the StopToken to enter the
`stop_requested()` state, but the sender may or may not respond in a timely manner.

Contrast this with the **Stream** concept (see below) that lazily produces the next
value only when the consumer asks for it, providing a natural backpressure mechanism.

## Sender vs ManySender

Whereas **Sender** produces a single result. ie. a single call to one of either
`set_value()`, `set_done()` or `set_error()`, a **ManySender** produces multiple values
via zero or more calls to `set_next()` followed by a call to either `set_value()`,
`set_done()` or `set_error()` to terminate the sequence.

A **Sender** is a kind of **ManySender**, just a degenerate ManySender that never
sends any elements via `set_next()`.

Also, a **ManyReceiver** is a kind of **Receiver**. You can pass a **ManyReceiver**
to a **Sender**, it will just never have its `set_next()` method called on it.

Note that terminal calls to a receiver (i.e. `set_value()`, `set_done()` or `set_error()`)
must be passed an rvalue-reference to the receiver, while non-terminal calls to a receiver
(i.e. `set_next()`) must be passed an lvalue-reference to the receiver.

The sender is responsible for ensuring that the return from any call to `set_next()`
**strongly happens before** the call to deliver a terminal signal is made.
ie. that any effects of calls to `set_next()` are visible within the terminal signal call.

A terminal call to `set_value()` indicates that the full-set of `set_next()` calls were
successfully delivered and that the operation as a whole completed successfully.

Note that the `set_value()` can be considered as the sentinel value of the parallel
tasks. Often this will be invoked with an empty pack of values, but it is also valid
to pass values to this `set_value()` call.
e.g. This can be used to produce the result of the reduce operation.

A terminal call to `set_done()` or `set_error()` indicates that the operation may have
completed early, either because the operation was asked to stop early (as in `set_done`)
or because the operation was unable to satisfy its post-conditions due to some failure
(as in `set_error`). In this case it is not guaranteed that the full set of values were
delivered via `set_next()` calls.

As with a **Sender** and **ManySender** you must call `connect()` to connect a sender
to it. This returns an **OperationState** that holds state for the many-sender operation.

The **ManySender** will not make any calls to `set_next()`, `set_value()`, `set_done()`
or `set_error()` before calling `start()` on the operation-state returned from
`connect()`.

Thus, a **Sender** should usually constrain its `connect()` operation as follows:
```c++
struct some_sender_of_int {
  template<typename Receiver>
  struct operation { ... };

  template<typename Receiver>
    requires
      value_receiver<std::decay_t<Receiver>, int> &&
      done_receiver<std::decay_t<Receiver>
  friend operation<std::decay_t<Receiver>> tag_invoke(
    tag_t<connect>, some_many_sender&& s, Receiver&& r);
};
```

While a **ManySender** should constrain its `connect()` opertation like this:
```c++
struct some_many_sender_of_ints {
  template<typename Receiver>
  struct operation { ... };

  template<typename Receiver>
    requires
      next_receiver<std::decay_t<Receiver>, int> &&
      value_receiver<std::decay_t<Receiver>> &&
      done_receiver<std::decay_t<Receiver>>
  friend operation<std::decay_t<Receiver>> tag_invoke(
    tag_t<connect>, some_many_sender&& s, Receiver&& r);
};
```

## Sequential vs Parallel Execution

A **ManySender**, at a high level, sends many values to a receiver.

For some use-cases we want to process these values one at a time and in
a particular order. ie. process them sequentially. This is largely the
pattern that the Reactive Extensions (Rx) community has built their
concepts around.

For other use-cases we want to process these values in parallel, allowing
multiple threads, SIMD lanes, or GPU cores to process the values more
quickly than would be possible normally.

In both cases, we have a number of calls to `set_next`, followed by a
call to `set_value`, `set_error` or `set_done`.
So what is the difference between these cases?

Firstly, the **ManySender** implementation needs to be _capable_ of making
overlapping calls to `set_next()` - it needs to have the necessary
execution resources available to be able to do this.
Some senders may only have access to a single execution agent and so
are only able to send a single value at a time.

Secondly, the receiver needs to be prepared to handle overlapping calls
to `set_next()`. Some receiver implementations may update shared state
with the each value without synchronisation and so it would be undefined
behaviour to make concurrent calls to `set_next()`. While other
receivers may have either implemented the required synchronisation or
just not require synchronisation e.g. because they do not modify
any shared state.

The set of possible execution patterns is thus constrained to the
intersection of the capabilities of the sender and the constraints
placed on the call pattern by the receiver.

Note that the constraints that the receiver places on the valid
execution patterns are analagous to the "execution policy" parameter
of the standard library parallel algorithms.

With existing parallel algorithms in the standard library, when you
pass an execution policy, such as `std::execution::par`, you are telling
the implementation of that algorithm the constraints of how it is
allowed to call the callback you passed to it.

For example:
```c++
std::vector<int> v = ...;

int max = std::reduce(std::execution::par_unseq,
                      v.begin(), v.end(),
                      std::numeric_limits<int>::min(),
                      [](int a, int b) { return std::max(a, b); });
```

Passing `std::execution::par` is not saying that the algorithm
implementation _must_ call the lambda concurrently, only that it _may_
do so. It is always valid for the algorithm to call the lambda sequentially.

We want to take the same approach with the **ManySender** / **ManyReceiver**
contract to allow a **ManySender** to query from the **ManyReceiver**
what the execution constraints for calling its `set_next()` method
are. Then the sender can make a decision about the best strategy to
use when calling `set_next()`.

To do this, we define a `get_execution_policy()` CPO that can be invoked,
passing the receiver as the argument, and have it return the execution
policy that specifies how the receiver's `set_next()` method is allowed
to be called.

For example, a receiver that supports concurrent calls to `set_next()`
would customise `get_execution_policy()` for its type to return
either `unifex::par` or `unifex::par_unseq`.

A sender that has multiple threads available can then call
`get_execution_policy(receiver)`, see that it allows concurrent execution
and distribute the calls to `set_next()` across available threads.

## TypedManySender

With the **TypedSender** concept, the type exposes type-aliases that allow
the consumer of the sender to query what types it is going to invoke a
receiver's `set_value()` and `set_error()` methods with.

A **TypedManySender** concept similarly extends the **ManySender**
concept, requiring the sender to describe the types it will invoke `set_next()`,
via a `next_types` type-alias, in addition to the `value_types` and `error_types`
type-aliases required by **TypedSender**.

Note that this requirement for a **TypedManySender** to provide the `next_types`
type-alias means that the **TypedSender** concept, which only need to provide the
`value_types` and `error_types` type-aliases, does not subsume the **TypedManySender**
concept, even though **Sender** logically subsumes the **ManySender** concept. 

# Streams

Streams are another form of asynchronous sequence of values where the
values are produced lazily and on-demand only when the consumer asks
for the next value by calling a `next()` method that returns a **Sender**
that will produce the next value.

A consumer may only ask for a single value at a time and must wait
until the previous value has been produced before asking for the next
value.

A stream has two methods:
- `next(stream)` - Returns a `Sender` that produces the next value.
  The sender delivers one of the following signals to the receiver
  passed to it:
  - `set_value()` if there is another value in the stream,
  - `set_done()` if the end of the stream is reached
  - `set_error()` if the operation failed
- `cleanup(stream)` - Returns a `Sender` that performs async-cleanup
  operations needed to unsubscribe from the stream.
  - Calls `set_done()` once the cleanup is complete.
  - Calls `set_error()` if the cleanup operation failed.

Note that if `next()` is called then it is not permitted to call
`next()` again until that sender is either destroyed or has been
started and produced a result.

If the `next()` operation completes with `set_value()` then the
consumer may either call `next()` to ask for the next value, or
may call `cleanup()` to cancel the rest of the stream and wait
for any resources to be released.

If a `next()` operation has ever been started then the consumer
must ensure that the `cleanup()` operation is started and runs
to completion before destroying the stream object.

If the `next()` operation was never started then the consumer
is free to destroy the stream object at any time.

## Differences to ManySender

This has a number of differences compared with a **ManySender**.

* The consumer of a stream may process the result asynchronously and can
  defer asking for the next value until it has finished processing the
  previous value.
  * A **ManySender** can continue calling `set_next()` as soon as the
    previous call to `set_next()` returns.
  * A **ManySender** has no mechanism for flow-control. The **ManyReceiver**
    must be prepared to accept as many values as the **ManySender** sends
    to it.
* The consumer of a stream may pass a different receiver to handle
  each value of the stream.
  * **ManySender** sends many values to a single receiver.
  * **Streams** sends a single value to many receivers.
* A **ManySender** has a single cancellation-scope for the entire operation.
  The sender can subscribe to the stop-token from the receiver once at the
  start of the operation.
  * As a stream can have a different receiver that will receiver each element
    it can potentially have a different stop-token for each element and so
    may need to subscribe/unsubscribe stop-callbacks for each element.

## Coroutine compatibility

When a coroutine consumes an async range, the producer is unable to send
the next value until the coroutine has suspended waiting for it. So an
async range must wait until a consumer asks for the next value before
starting to compute it.

A **ManySender** type that continuously sends the next value as soon as
the previous call to `set_value()` returns would be incompatible with
a coroutine consumer, as it is not guaranteed that the coroutine consumer
would necessarily have suspended, awaiting the next value.

A stream is compatible with the coroutine model of producing a stream
of values. For example the `cppcoro::async_generator` type allows the
producer to suspend execution when it yields a value. It will not resume
execution to produce the next value until the consumer finishes processing
the previous value and increments the iterator.

## Design Tradeoffs

The stream design needs to construct a new operation-state object for requesting
each value in the stream.

The setup/teardown of these state-machines could potentially be expensive
if we're doing it for every value compared to a **ManySender** that can make
many calls to a single receiver.

However, this approach more closely matches the model that naturally fits with
coroutines.

It separates the operations of cancelling the stream in-between requests
for the next element (ie. by calling `cleanup()` instead of `next()`)
from the operaiton of interrupting an outstanding request to `next()` using
the stop-token passed to that `next()` operation.

A consumer may not call `next()` or `cleanup()` until the prior call to
`next()` has completed. This means that implementations of `cleanup()`
often do not require thread-synchronisation as the calls are naturally
executed sequentially.

# Scheduler

A scheduler is a lightweight handle that represents an execution context
on which work can be scheduled.

A scheduler provides a single operation `schedule()` that is an async operation
(ie. returns a sender) that logically enqueues a work item when the operation
is started and that completes when the item is dequeued by the execution context.

If the schedule operation completes successfully (ie. completion is signalled
by a call to `set_value()`) then the operation is guaranteed to complete on
the scheduler's associated execution context and the `set_value()` method
is called on the receiver with no value arguments.
ie. the schedule operation is a "sender of void".

If the schedule operation completes with `set_done()` or `set_error()` then
it is implementation defined which execution context the call is performed
on.

The `schedule()` operation can therefore be used to execute work on the
scheduler's associated execution context by performing the work you want to
do on that context inside the `set_value()` call.

A scheduler concept would be defined:
```c++
namespace unifex
{
  // The schedule() CPO
  inline constexpr unspecified schedule = {};

  // The scheduler concept.
  template<typename T>
  concept scheduler =
    std::is_nothrow_copy_constructible_v<T> &&
    std::is_nothrow_move_constructible_v<T> &&
    std::destructible<T> &&
    std::equality_comparable<T> &&
    requires(const T cs, T s) {
      schedule(cs); // TODO: Constraint this returns a sender of void.
      schedule(s);
    };
}
```

## Sub-schedulers

If you want to schedule work back on the same execution context then you
can use the `schedule_with_subscheduler()` function instead of `schedule()`
and this will call `set_value()` with a **Scheduler** that represents the current
execution context.

e.g. on a thread-pool the sub-scheduler might represent a scheduler that lets
you directly schedule work onto a particular thread rather than to the thread
pool as a whole.

This allows the receiver to schedule additional work onto the same execution
context/thread if desired.

The default implementation of `schedule_with_subscheduler()` just produces
a copy of the input scheduler as its value.

# TimeScheduler

A **TimeScheduler** extends the concept of a **Scheduler** with the ability to
schedule work to occur at or after a particular point in time rather than
as soon as possible.

This adds the following capabilities:
* `typename TimeScheduler::time_point`
* `now(ts) -> time_point`
* `schedule_at(ts, time_point) -> sender_of<void>`
* `schedule_after(ts, duration) -> sender_of<void>`

Instead, the current time is obtained from the scheduler itself by calling the `now()`
customisation point, passing the scheduler as the only argument.

This allows tighter integration between scheduling by time and the progression of time
within a scheduler. e.g. a time scheduler only needs to deal with a single time source
that it has control over. It doesn't need to be able to handle different clock sources
which may progress at different rates.

Having the `now()` operation as an operation on the `TimeScheduler`  allows implementations of schedulers that contain stateful clocks such as virtual
time schedulers which can manually advance time to skip idle periods. e.g. in unit-tests.

```c++
namespace unifex
{
  // TimeScheduler CPOs
  inline constexpr unspecified now = unspecified;
  inline constexpr unspecified schedule_at = unspecified;
  inline constexpr unspecified schedule_after = unspecified;

  template<typename T>
  concept time_scheduler =
    scheduler<T> &&
    requires(const T scheduler) {
      now(scheduler);
      schedule_at(scheduler, now(scheduler));
      schedule_after(scheduler, now(scheduler) - now(scheduler));
    };
}
```

# TimePoint concept

A TimePoint object used to represent a point in time on the timeline of a given TimeScheduler.

Note that `time_point` here may be, but is not necessarily a `std::chrono::time_point`.

The TimePoint concept offers a subset of the capabilities of `std::chrono::time_point`.
In particular it does not necessary provide a `clock` type and thus does not necessarily
provide the ability to call a static `clock::now()` method to obtain the current time.

The current time is, instead, obtained from a **TimeScheduler** object using the `now()` CPO.

You must be able to calculate the difference between two time-point values to produce a
`std::chrono::duration`. And you must be able to add or subtract a `std::chrono::duration`
from a time-point value to produce a new time-point value.

```c++
namespace unifex
{
  template<typename T>
  concept time_point =
    std::regular<T> &&
    std::totally_ordered<T> &&
    requires(T tp, const T ctp, typename T::duration d) {
      { ctp + d } -> std::same_as<T>;
      { ctp - d } -> std::same_as<T>;
      { ctp - ctp } -> std::same_as<typename T::duration>;
      { tp += d } -> std::same_as<T&>;
      { tp -= d } -> std::same_as<T&>;
    };
```

NOTE: This concept is only checking that you can add/subtract the same duration
type returned from `operator-(T, T)`. Ideally we'd be able to check that this
type supports addition/subtraction of any `std::chrono::duration` instantiation.

# StopToken concept

To support cancellation of asynchronous operations that may be executing concurrently
Unifex makes use of stop-tokens.

A stop-token is a token that can be passed to an operation and that can be later
used to communicate a request for that operation to stop executing, typically
because the result of the operation is no longer needed.

In C++20 a new `std::stop_token` type has been added to the standard library.
However, in Unifex we also wanted to support other kinds of stop-token that
permit more efficient implementations in some cases. For example, to avoid the
need for reference-counting and heap-allocation of the shared-state in cases
where structured concurrency is being used, or to avoid any overhead altogether
in cases where cancellation is not required.

To this end, Unifex operations are generally written against a generic
StopToken concept rather than against a concrete type, such as `std::stop_token`.

The StopToken concept defines the end of a stop-token passed to an async
operation. It does not define the other end of the stop-token that is used
to request the operation to stop.

```c++
namespace unifex
{
  struct __stop_token_callback_archetype {
    // These have no definitions.
    __stop_token_callback_archetype() noexcept;
    __stop_token_callback_archetype(__stop_token_callback_archetype&&) noexcept;
    __stop_token_callback_archetype(const __stop_token_callback_archetype&) noexcept;
    ~__stop_token_callback_archetype();
    void operator()() noexcept;
  };

  template<typename T>
  concept stop_token_concept =
    std::copyable<T> &&
    std::is_nothrow_copy_constructible_v<T> &&
    std::is_nothrow_move_constructible_v<T> &&
    requires(const T token) {
      typename T::template callback_type<__stop_token_callback_archetype>;
      { token.stop_requested() ? (void)0 : (void)0 } noexcept;
      { token.stop_possible() ? (void)0 : (void)0 } noexcept;
    } &&
    std::destructible<
      typename T::template callback_type<__stop_token_callback_archetype>> &&
    std::is_nothrow_constructible_v<
      typename T::template callback_type<__stop_token_callback_archetype>,
      T, __stop_token_callback_archetype> &&
    std::is_nothrow_constructible_v<
      typename T::template callback_type<__stop_token_callback_archetype>,
      const T&, __stop_token_callback_archetype>;
}
```

NOTE: The C++20 `std::stop_token` type does not actually satisfy this concept as it
does not have the nested `callback_type` template type alias. We may instead need to
define some customisation point for constructing the stop-callback object instead
of using a nested type-alias.
