# Index

* Receiver Queries
  * `get_stop_token()`
  * `get_scheduler()`
  * `get_allocator()`
  * `get_execution_policy()`
* Sender Factories
  * `create`
  * `just()`
  * `just_done()` / `stop()`
  * `just_error()`
  * `just_void_or_done()`
  * `stop_if_requested()`
* Sender Algorithms
  * `detach_on_cancel()`
  * `then()`
  * `finally()`
  * `via()`
  * `typed_via()`
  * `on()`
  * `let_value()`
  * `let_error()`
  * `let_done()`
  * `let_value_with()`
  * `let_value_with_stop_source()`
  * `let_value_with_stop_token()`
  * `sequence()`
  * `sync_wait()`
  * `when_all()`
  * `materialize()`
  * `dematerialize()`
  * `repeat_effect_until()`
  * `repeat_effect()`
  * `retry_when()`
  * `stop_when()`
  * `allocate()`
  * `with_query_value()`
  * `with_allocator()`
  * `done_as_optional()`
  * `nest()`
* Sender Types
  * `async_trace_sender`
* Sender Queries
  * `blocking()`
* Many Sender Algorithms
  * `bulk_transform()`
  * `bulk_join()`
  * `bulk_schedule()`
* Stream Algorithms
  * `adapt_stream()`
  * `next_adapt_stream()`
  * `reduce_stream()`
  * `for_each()`
  * `transform_stream()`
  * `via_stream()`
  * `typed_via_stream()`
  * `on_stream()`
  * `type_erase<Ts...>()`
  * `take_until()`
  * `single()`
  * `stop_immediately()`
  * `delay()`
* Stream Types
  * `range_stream`
  * `type_erased_stream<Ts...>`
  * `never_stream`
* Scheduler Types
  * `inline_scheduler`
  * `single_thread_context`
  * `trampoline_scheduler`
  * `timed_single_thread_context`
  * `thread_unsafe_event_loop`
  * `new_thread_context`
  * `linux::io_uring_context`
* StopToken Types
  * `unstoppable_token`
  * `inplace_stop_token` / `inplace_stop_source`
* Synchronisation Primitives
  * `async_manual_reset_event`
  * `async_mutex`
* Coroutine support
  * `task`
  * `at_coroutine_exit`
* Other
  * `async_scope`

# Receiver Queries

### `get_scheduler(receiver)`

A query that can be used to obtain the associated scheduler from the receiver.

This can be used by senders to obtain a scheduler that can be used to schedule
work if required.

Receivers can customise this CPO to return the current scheduler.

See the `schedule()` algorithm, which schedules onto the current scheduler.

### `get_allocator(receiver)`

Obtain the current allocator that should be used for heap-allocating storage
needed by the implementation of a sender if required.

This may be customised by a receiver to return a specific allocator but if
it has not been customised then defaults to return `std::allocator<char>`.

### `get_stop_token(receiver)`

Obtain the current stop-token from the receiver.

If a sender's operation is able to be cancelled/interrupted then the sender should
call this function to query the stop-token provided by the receiver and use
this stop-token to either poll or subscribe for notification of a request to stop.

If a receiver has not customised this it will default to return `unstoppable_token`.

See the [Cancellation](cancellation.md) section for more details on cancellation.

### `get_execution_policy(manyReceiver)`

For a ManyReceiver, obtains the execution policy object that specifies the constraints
on how a ManySender is allowed to call `set_next()`.

The following execution policies are built-in and understood by the many-sender
algorithms in libunifex.

* `unifex::sequenced_policy` - Calls to `set_next()` on the receiver must be sequenced
  and may not be executed concurrently on different threads or have their executions
  interleaved on a single thread.

* `unifex::unsequenced_policy` - Calls to `set_next()` are safe to be interleaved
  with each other on the same thread but are not safe to be executed concurrently
  on different threads. This typically allows vectorised execution of the calls using
  SIMD instructions.

* `unifex::parallel_policy` - Calls to `set_next()` are safe to be executed
  concurrently on different threads, but are not safe to be interleaved on
  a given thread. Use this if the forward-progress of one call to `set_next()`
  may be dependent on another call to `set_next()` making forward progress.
  e.g. if multiple calls attempt to acquire a lock on the same mutex.

* `unifex::parallel_unsequenced_policy` - Calls to `set_next()` are safe to
  be executed concurrently on different threads and are also safe to have
  their executions interleaved on a given thread.

Note that, while it is possible to extend the set of execution policies with
application-specific policies, builtin implementations of bulk algorithms
will not necessarily understand them and will treat them as if they were
the `sequenced_policy`.

If a receiver does not customise the `get_execution_policy()` CPO then it
will default to returning the `sequenced_policy`.

# Sender Factories

### `create<ValueTypes...>(callable)`

_Synopsis:_ A utility for building a sender-based async API out of a C-style async API
that accepts a `void*` context and a callback.

_Example:_
```c++
// A void-returning C-style async API that accepts a context and a continuation:
using callback_t = void(void* /*context*/, int /*result*/);
void old_c_style_api(int a, int b, void* context, callback_t* callback_fn);

// A sender-based async API implemented in terms of the C-style API (using C++20):
unifex::typed_sender auto new_sender_api(int a, int b) {
  return unifex::create<int>([=](auto& rec) {
    old_c_style_api(a, b, &rec, [](void* context, int result) {
      unifex::void_cast<decltype(rec)>(context).set_value(result);
    });
  });
}
```

`ValueTypes...` is a pack representing the value types of the resulting sender. It should
be the list of value type(s) accepted by the callback (with the exception of the `void*`
context). In the above example, since `callback_t` accepts an `int` as the result of the
async computation, we pass `int` as the template argument to `create`.

The first argument to `create` is a `void`-returning callable that accepts an lvalue
reference to an object whose type satisfies the `unifex::receiver_of<ValueTypes...>`
concept. This function should dispatch to the C-style callback (see example).

The second argument is an optional extra bit of data to be bundled with the receiver
passed to the callable. E.g., if the first argument to `create` is a lambda that accepts
`(auto& rec)` and the second argument is `42`, then from within the body of the lambda,
the value of the expression `rec.context()` is `42`.

`create` returns a typed sender that, when connected and started, dispatches to the
wrapped C-style API with the callback of your choosing. The receiver passed to the
callable wraps the receiver passed to `connect`. The callback should "complete" the
receiver passed to the callable, which will complete the receiver passed to `connect` in
turn.

### `just(args...)`

Returns a sender that completes synchronously by calling `set_value()`
with `args...`.

### `just_done()` / `stop()`

Returns a sender that completes synchronously by calling `set_done()`.

### `just_error(e)`

Returns a sender that completes synchronously by calling `set_error()` with `e`.

### `just_void_or_done(isVoid)`

Returns a sender that completes synchronously by calling `set_value(void)` if
`isVoid == true` or calling `set_done()` otherwise.

### `just_from(callable)`

Returns a sender that completes synchronously by calling `set_value()` with
the result of invoking the callable with no arguments. If the callable returns
`void`, then the sender completes synchronously by first invoking the callable
and then calling `set_value()` with no arguments.

If the invocation of the callable exits with an exception, the exception is
caught and passed to the receiver's `set_error` with `std::current_exception()`.

`just_from(callable)` is synonymous with `then(just(), callable)`.

### `stop_if_requested()`

Returns a sender that queries the receiver with `get_stop_token()`, tests the
resulting stop token with `stop_requested()`. If the result is `true`, then
the sender completes synchronously with `set_done()`. Otherwise, the sender
completes synchronously by calling `set_value()` with no arguments.

### `defer(callable)`

Accepts a callable that returns a sender. Returns a sender that, when it
is started, invokes the callable and connects and starts the returned sender.

If the invocation of the callable exits with an exception, the exception is
caught and passed to the receiver's `set_error` with `std::current_exception()`.

`defer(callable)` is synonymous with `let_value(just(), callable)`.

# Sender Algorithms

### `detach_on_cancel(Sender sender) -> Sender`

Takes a Sender and produces a new Sender that will heap-allocate its operation
state for the purpose of detaching a slow operation upon request to stop.

Completion of the `sender` is otherwise delegated to the new Sender.

### `then(Sender predecessor, Func func) -> Sender`

Returns a sender that transforms the value of the `predecessor` by calling
`func(value)`.

For example:
```c++
then(some_operation(),
    [](auto& x) {
      return func(x);
    });
```
is roughly equivalent to the following coroutine code:
```c++
{
  auto x = co_await some_operation();
  func(x);
}
```

### `let_value(Sender pred, Invocable func) -> Sender`

The `let_value()` algorithm accepts a predecessor task that produces a value that
you want remain alive for the duration of a successor operation.

When the predecessor operation completes with a value, the function `func`
is invoked with lvalue references to copies of the values produced by
the predecessor. This invocation must return a Sender.

The references passed to `func` remain valid until the returned sender
completes, at which point the variables go out of scope.

For example:
```c++
let_value(some_operation(),
    [](auto& x) {
      return other_operation(x);
    });
```
is roughly equivalent to the following coroutine code:
```c++
{
  auto x = co_await some_operation();
  co_await other_operation(x);
}
```

If the predecessor completes with value then the `let_value()` operation as a
whole will complete with the result of the successor.

If the predecessor completes with done/error then `func` is not invoked
and the operation as a whole completes with that done/error signal.

### `let_error(Sender predecessor, Func func) -> Sender`

Returns a sender that calls `auto finalSender = func()` in `set_error()` and then
starts the returned `finalSender`. This allows a call to `set_error` to be
delayed, to be transformed into an done signal or a value, etc..

### `let_done(Sender predecessor, Func func) -> Sender`

Returns a sender that calls `auto finalSender = func()` in `set_done()` and then
starts the returned `finalSender`. This allows a call to `set_done` to be
delayed, to be transformed into an error or a value, etc..

### `let_value_with(Invocable state_factory, Invocable func) -> Sender`

The `let_value_with()` algorithm accepts an invocable that produces a value that
you want remain alive for the duration of a successor operation.

When the `let_value_with` sender is connected the invocable is called to construct
the result in-place in the operation state.
In-place construction of the result is where `let_value_with` differs from `let_value`
in that the result of `state_factory` can be a non-moveable type, such as
`std::atomic` that will be constructed in-place in the operation state.

The references passed to `func` remain valid until the returned sender
completes, at which point the variables go out of scope.

For example:
```c++
let_value_with(
    some_factory,
    [](auto& x) {
      return other_operation(x);
    });
```
is roughly equivalent to the following coroutine code:
```c++
{
  auto x = some_factory();
  co_await other_operation(x);
}
```

If `state_factory` returns successfully then the `let_value_with()` operation
as a whole will complete with the result of the successor.

If `state_factory()` completes with an exception then the exception will
propagate out of the `connect` operation.

### `let_value_with_stop_source(Invocable func) -> Sender`

The `let_value_with_stop_source()` algorithm constructs an
`inplace_stop_token` that remains alive for the duration of an operation.

`func` is invoked with an lvalue reference to an `inplace_stop_source`
derived from the `inplace_stop_token`. This invocation must return a
Sender.

The `inplace_stop_token` is provided to the `Sender` returned by `func`
via a call to `get_stop_token` on the provided `Receiver`.

The reference passed to `func` remain valid until the returned sender
completes, at which point the `inplace_stop_token` goes out of scope.

For example:
```c++
let_value_with_stop_source(
    [](unifex::inplace_stop_source& stop_source) {
      return other_operation(stop_source);
    });
```

Calling `.request_stop()` on the stop-source passed to the function requests
cancellation of the operation returned by the function. Note that cancellation
may also be requested through the stop-token of the receiver that is connected
to the sender returned by `let_value_with_stop_source()`.

### `let_value_with_stop_token(Invocable func) -> Sender`

The `let_value_with_stop_token()` algorithm takes a function object that is
invoked at `unifex::connect()` time with an `inplace_stop_token` object that
can be used to receive a stop-request sent by the parent operation through the
receiver it passes to connect().

The function invocation must return a Sender which is immediately connected.
The result of the sender returned from the function becomes the result of the
`let_value_with_stop_token()` sender.

The stop-token passed to the function is only guaranteed to be valid until
the sender returned by the function completes.

For example:
```c++
let_value_with_stop_token(
    [](unifex::inplace_stop_token stop_token) {
      return other_operation(stop_token);
    });
```

### `finally(Sender source, Sender completion) -> Sender`

Returns a sender that will first launch `source` and upon completion of
`source` will launch the `completion` sender.

If `completion` completes with `set_value()` (which must complete with an
empty value pack) then the composed operation completes with the result of
`source`.
Otherwise, if `completion` sender completes with `set_done` or `set_error`
then the composed operation completes with the result of `completion`.

The composed finally-operation will complete inline on the execution context
that the `completion` sender completes on, except in the case that the call
to `connect()` on the completion-sender exits with an exception, in which case
the operation will complete with `set_error()` inline on whatever execution
context the `source` sender completed on.

Note that `completion` sender must complete with an empty value pack
if it completes with `set_value`.
ie. it must be a `void`-value sender.

### `via(Scheduler scheduler, Sender sender) -> Sender`

Returns a sender that produces the result from `sender` on the
execution context associated with `scheduler`.

If the result of `schedule(scheduler)` completes with `set_done()` then
`set_done()` is sent. If the result of `schedule(scheduler)` completes with
`set_error()` then its error is sent. Otherwise sends the result of `sender`.

### `typed_via(Sender source, Scheduler scheduler) -> Sender`

Returns a sender that produces the result from `source`, which must
declare the nested `value_types`/`error_types` type aliases which describe which
overloads of `set_value()`/`set_error()` they will call, on the execution context
associated with `scheduler`.

### `on(Scheduler scheduler, Sender sender) -> Sender`

Returns a sender that ensures that `sender` is started on the
execution context associated with the specified `scheduler`.

The `sender` is executed with a receiver that customises the
`get_scheduler` query to return the specified `scheduler`.

The default implementation schedules the call to `connect()`
and subsequent `start()` onto an execution context associated
with `scheduler` using the `schedule(scheduler)` operation.

If `schedule(scheduler)` completes with `set_done()` or
`set_error()` then the `on()` operation completes with
that signal and never starts executing `sender`.

The `on()` algorithm may be customised by particular schedulers
and/or scheduler+sender combinations to provide an alternative
impllementation.

### `sequence(Sender... predecessors, Sender last) -> Sender`

The `sequence()` algorithm takes a variadic pack of senders and executes
them sequentially, only starting the next sender if/when the previous sender
completed successfully (ie. with `set_value`).

All but the `last` sender must produce a `void` value result
i.e. call `set_value(receiver)` with no additional value args.

If any of the input senders complete with `set_done` or `set_error`
then the operation as a whole completes with that signal and
any subsequent operations in the sequence are not started.

This algorithm may be customised by defining a custom `tag_invoke(tag_t<sequence>, ...)`
overload for your particular sender types. You can either provide a customisation
for a variadic pack of senders or for a pair of senders.

If you provide a customisation for a pair of senders then this customisation
will be applied to the first two arguments and then reinvoke `sequence()`
with the first two arguments replaced with the result of `sequence(first, second)`.

### `sync_wait(Sender sender) -> std::optional<Result>`

Blocks the current thread waiting for the specified sender to complete.

Returns a non-empty optional if it completed with `set_value()`.
Or `std::nullopt` if it completed with `set_done()`
Or throws an exception if it completed with `set_error()`

### `when_all(Senders...) -> Sender`

Takes a variadic number of senders and returns a sender that launches each of
the input senders in-turn without waiting for the prior senders to complete.
This allows each of the input senders to potentially execute concurrently.

The result of the Sender has a value_type of:
`std::tuple<std::variant<std::tuple<Ts...>, ...>, ...>`

There is an element in the outer-tuple for each input sender.
Each element in the outer tuple is a variant that indicates which overload
of `value()` was called on the receiver by the corresponding sender.
The variant's value is a tuple that contains copies of the arguments passed
to `value()`.

If any of the input senders complete with done or error then it will request
any senders that have not yet completed to stop and the operation as a whole
will complete with done or error.

### `materialize(Sender sender) -> Sender`

Materializes the completion signal of `sender` into the value-channel by
invoking prepending the completion arguments with the corresponding
`set_value`, `set_error` or `set_done` CPO as an additional argument.

ie. Transforms the following LHS completion signals to the RHS completion signals
* `set_value(r, values...)` -> `set_value(r, set_value, values...)`
* `set_error(r, e)` -> `set_value(r, set_error, e)`
* `set_done(r)` -> `set_value(r, set_done)`

This allows you to treat any result as a success and process the result as
a value.

### `dematerialize(Sender sender) -> Sender`

Converts a sender of materialized signals into a sender of those signals.
This reverses the transformation of signals performed by `materialize()`.

If `sender` completes with `set_value(r, set_value, values...)` then the
dematerialized sender will complete with `set_value(r, values...)`.

Similarly if `sender` completes with `set_value(r, set_error, e)` then the
dematerialized sender will complete with `set_error(r, e)`.

And if `sender` completes with `set_value(r, set_done)` then the dematerialized
sender will complete with `set_done(r)`.

Any `set_error()` or `set_done()` signals are passed through unchanged.

### `repeat_effect_until(Sender source, Invocable predicate) -> Sender`

The `repeat_effect_until()` algorithm repeats the source sender for as long as the
predicate returns false.

The `source` sender must be lvalue connectable (ie. can be connected and started
multiple times).

The `source` sender must be an effect. It must produce void.

If the `source` sender completes with `set_error()` or `set_done()` then the
`repeat_effect_until()` operation completes with that same signal.

If the `source` sender completes with void then the `predicate` function is
invoked. The `predicate` function must return `false` to repeat the source and
`true` to complete with void.

If the invocation of the `predicate()` throws an exception then the
`repeat_effect_until()` operation immediately completes with
`set_error(std::current_exception())`.

Example usage: Repeat the operation forever - until the source is cancelled.
```c++
unifex::repeat_effect_until(
  some_operation(),
  [] {
    return false;
  });
```

This is the default implementation for `repeat_effect()`.

### `repeat_effect(Sender source) -> Sender`

The `repeat_effect()` algorithm repeats the source sender until the source is
cancelled.

The `source` sender must be lvalue connectable (ie. can be connected and started
multiple times).

The `source` sender must be an effect. It must produce void.

If the `source` sender completes with `set_error()` or `set_done()` then the
`repeat_effect()` operation completes with that same signal.

If the `source` sender completes with void then the source is started again.

Example usage: Repeat the operation forever - until the source is cancelled.
```c++
unifex::repeat_effect(some_operation());
```

The default implementation uses `repeat_effect_until()` with a predicate that
always returns false.

### `retry_when(Sender source, Invocable<Error> handler) -> Sender`

The `retry_when()` algorithm repeatedly retries executing the input sender
if it fails with an error after some delay indicated by the handler function.

The `source` sender must be lvalue connectable (ie. can be connected and started
multiple times).

If the `source` sender completes with `set_value()` or `set_done()` then the
`retry_when()` operation completes with that same signal.

If the `source` sender completes with an error then the `handler` function is
invoked with that error value. The `handler` function must return a new sender
which is then immediately started.

If the invocation of the `handler()` throws an exception or attempting to launch
the returned sender throws an exception then the `retry_when()` operation immediately
completes with `set_error(std::current_exception())`.

If the sender returned by `handler()` completes with `set_value()` then the
`source` operation is relaunched.

Otherwise, if the sender returned by `handler()` completes with `set_error(e)` or
`set_done()` then this becomes the result of the `retry_when()` operation.


Example usage: Retry the operation up to 5 times with increasing delays between retries.
```c++
unifex::retry_when(
  some_operation(),
  [count = 0, scheduler](std::exception_ptr ex) mutable {
    if (++count >= 5) {
      std::rethrow_exception(ex);
    }
    return unifex::schedule_after(scheduler, count * 50ms);
  });
```

### `stop_when(Sender source, Sender trigger) -> Sender`

Returns a sender that will start both source and trigger and will cancel the
other one whenever the first of the two senders completes.

Completes with the result of `source` once both `source` and `trigger` senders
have completed. The result is produced inline on the execution context of whichever
sender completed second.

Example usage:
```c++
// A simple timeout that cancels an operation after 200ms
unifex::stop_when(
  some_operation(),
  unifex::schedule_after(200ms));
```

### `allocate(Sender sender) -> Sender`

Takes a Sender and produces a new Sender that will heap-allocate its operation
state rather than embedding its operation state into the parent operation-state.

This can be used to avoid bloating parent operation-state objects with a large
child operation-state that might only be used part of the time.

Uses the allocator returned by `get_allocator(receiver)`.

The allocator to be used can be customised by injecting an allocator using the
`with_allocator()` algorithm.

### `with_query_value(Sender sender, CPO cpo, T value) -> Sender`

Wraps `sender` in a new sender that will pass a receiver to `connect()`
on `sender` that customises CPO to return the specified value.

This can be used to inject contextual information into child operations.

For example:
```c++
inline constexpr unspecified get_some_property = {}; // Some CPO

sender auto some_async_operation() { ... }

sender auto inject_context() {
  // Inject the value '42' as the result of 'get_some_property()' when queried
  // by child operations of some_async_operation().
  return with_query_value(some_async_operation(), get_some_property, 42);
}
```

### `with_allocator(Sender sender, Allocator allocator) -> Allocator`

Wraps `sender` in a new sender that will injects `allocator` as the
result of `get_allocator()` query on receivers passed to child operations.

Child operations should use this allocator to perform heap allocations.

### `done_as_optional(Sender sender) -> Sender`

`done_as_optional` is used to handle a done signal by mapping it into the
value channel as an empty `std::optional`. The value channel is also converted
into an optional. The result is a sender that never completes with done,
reporting cancellation by completing with an empty optional.

This function only accepts `typed_sender`s that complete with either
`void` or a single type.

For example:
```c++
task<int> f();

task<void> g() {
  std::optional<int> i = co_await done_as_optional(f());
  if (i) {
    // OK, f() completed successfully and wasn't cancelled
  } else {
    // f() was cancelled before it finished.
  }
}
```

### `nest(Sender sender, Scope& scope) -> Sender`

`nest` registers the given *Sender* with the given "scope", which should be of a
type that works like `v1::async_scope` or `v2::async_scope`. A *Sender* that has
been registered with a scope will prevent that scope from being joined until the
*Sender* has either been discarded or executed.

## Sender Types

### `async_trace_sender`

A sender that will produce the current async stack-trace containing the
chain of continuations for the current async operation.

The stack-trace is represented as a `std::vector<async_trace_entry>` where
the `async_trace_entry` is defined as follows:

```c++
struct async_trace_entry {
  size_t depth; // depth of this trace entry from the starting point.
  size_t parentIndex; // index into vector of the parent continuation
  continuation_info continuation; // description of this continuation
};
```

## Sender Queries

### `blocking(const Sender&) -> blocking_kind`

Returns `blocking_kind::never` if the receiver will never be called on the
current thread before `start()` returns.

Returns `blocking_kind::always` if the receiver is guaranteed to be called
on some thread strongly-happens-before `start()` returns.
ie. the caller of `start()` can rely on the receiver having been called
after the `start()` method returns.

Returns `blocking_kind::always_inline` if the receiver is guaranteed to be
called inline on the current thread before `start()` returns.

Otherwise returns `blocking_kind::maybe`.

Senders can customise this algorithm by providing an overload of
`tag_invoke(tag_t<blocking>, const your_sender_type&)`.

## Many Sender Algorithms

### `bulk_transform(ManySender sender, Func func, FuncPolicy policy) -> ManySender`

For each `set_next(values...)` result produced by `sender`, invokes
`func(values...)` and produces the result of that call as its `set_next()`
result.

The `policy` argument is optional and if absent, defaults to `get_execution_policy(func)`.

The resulting execution policy incorporates the union of the constraints
placed on the execution of the function and the execution of the
downstream receiver's `set_next()` method.

i.e. both the down-stream ManyReceiver's execution policy and the function's
execution policy must allow parallel execution for the bulk_transform
operation to permit parallel execution. Same for unsequenced execution.

This algorithm is transparent to `set_value()`, `set_error()` and `set_done()`
completion signals.

### `bulk_join(ManySender source) -> Sender`

Joins a bulk operation on a ManySender and turns it into a SingleSender
operation that completes once all of the `set_next()` calls have completed.

The input `source` sender must be a ManySender of `void` (ie. no values passed
to `set_next()`).

The returned single-sender is transparent to the `set_value()`, `set_error()`
and `set_done()` signals.

### `bulk_schedule(Scheduler sched, Count n) -> ManySender`

Returns a ManySender of type `Count` that sends the values `0 .. n-1`
to the receiver's `set_next()` channel.

The default implementation of this algorithm schedules a single
task onto the specified scheduler using `schedule()` and then calls
`set_next()` in a loop.

Scheduler types are permitted to customise the `bulk_schedule()` operation
to allow more efficient implementations. e.g. a thread-pool may choose to
split the work up into M pieces to execute across M different threads.

Note that customisations must still adhere to the constraints placed on
valid executions of `set_next()` according to the execution policy returned
from `get_execution_policy()`.

## Stream Algorithms

### `adapt_stream(Stream stream, Func adaptor) -> Stream`

Applies `adaptor()` to `next(stream)` and `cleanup(stream)` senders.

### `adapt_stream(Stream stream, Func nextAdaptor, Func cleanupAdaptor) -> Stream`

Applies `nextAdaptor()` to `next(stream)` and
applies `cleanupAdaptor()` to `cleanup(stream)`.

### `next_adapt_stream(Stream stream, Func adaptor) -> Stream`

Applies `adaptor()` to `next(stream)` only.
The `cleanup(stream)` Sender is passed through unchanged.

### `reduce_stream(Stream stream, T initialState, Func reducer) -> Sender<T>`

Applies `state = func(state, value)` for each value produced by `stream`.
Returns a Sender that returns the final value.

### `for_each(Stream stream, Func func) -> Sender<void>`

Executes `func(value)` for each value produced by stream.
Returned sender completes with `set_value()` once end of stream is reached.

Stream types can customise this algorithm via ADL by providing an overload
of `tag_invoke(tag_t<for_each>, your_stream_type, Func)`.

### `transform_stream(Stream stream, Func func) -> Stream`

Returns a stream that produces values that are the result of calling
`func(value)` on each value produced by the input stream.

### `via_stream(Scheduler scheduler, Stream stream) -> Stream`

Returns a stream that calls the receiver methods on the specified scheduler's
execution context.

Note that this works with streams that do not declare the types that they
send, but incurs a heap-allocation per value.

### `typed_via_stream(Scheduler scheduler, Stream stream) -> Stream`

Returns a stream that calls the receiver methods on the specified
scheduler's execution context.

This differs from `via_stream()` in that it requires that the stream
declares what overloads of `set_value()` and `set_error()` it will call by
providing the `value_types`/`error_types` type aliases.

### `on_stream(Scheduler scheduler, Stream stream) -> Stream`

Returns a stream that ensures `next(stream)` is started on the specified
scheduler's execution context.

### `type_erase<Ts...>(Stream stream) -> type_erased_stream<Ts...>`

Type-erases the stream.
Stream must produce value packs of type `(Ts...,)`.

### `take_until(Stream source, Stream trigger) -> Stream`

Returns a stream that will produce values from 'source' until the 'trigger'
stream produces any of value/error/done.

### `single(Sender sender) -> Stream`

Returns a stream that will produce the result of `sender` as the result
of the first element of the stream. If this is a 'value' then it will
produce `done()` as the second element of the stream.

### `stop_immediately<Ts...>(Stream stream) -> Stream`

Returns a stream that will immediately send `set_done()` from a pending `next()`
when stop is requested on the provided stop-token.

The request to stop will be passed on to the upstream `next()` call but
it will not wait for that stream to respond to cancellation before sending
`set_done()`.

The abandoned `next(stream)` call will be waited-for by the `cleanup(stream)`.

Any `set_value()` produced by an abandoned `next()` call is discarded.
Any `set_error()` produced by an abandoned `next()` call is reported in
the `cleanup()` result.

### `delay(Stream stream, TimeScheduler scheduler, Duration d) -> Stream`

Adapts `stream` to produce a new stream that delays the delivery of each
value, done and error signal by the specified duration.

## Stream Types

### `range_stream`

Produces a sequence of `int` values within a given range.
Mainly used for testing purposes.

### `type_erased_stream<Ts...>`

A type-erased stream that produces a sequence of value packs of type `(Ts, ...)`.
ie. calls to `set_value()` will be passed arguments of type `Ts&&...`

### `never_stream`

A stream whose `next()` completes with `set_done()` once when stop is requested.

Note that using this stream with a stop-token where `stop_possible()` returns
`false` will result in a memory-leak. The `next()` operation will never
complete.

## Scheduler Algorithms

### `schedule(Scheduler schedule) -> SenderOf<void>`

This is the basis operation for a scheduler.

The `schedule` operation returns a sender that is a lazy async operation.

A schedule operation logically enqueues an item onto the scheduler's queue when `start()`
is called and the operation completes when some thread associated with the scheduler's
execution context dequeues that item.

The operation signals completion by invoking either the `set_value()`,
`set_done()` or `set_error()` methods on the receiver passed to `connect()`.

As the operation completes on the execution context, the `set_value()` method by definition
be called on that execution context. Applications can therefore use the `schedule()`
operation to execute logic on the associated execution context by placing that logic within
the body of `set_value()`.

### `schedule() -> SenderOf<void>`

This is like `schedule(scheduler)` above but uses the implicit scheduler
obtained from the receiver passed to `connect()` by a calling `get_scheduler(receiver)`.

## Scheduler Types

### `inline_scheduler`

The `schedule()` operation immediately invokes the receiver inline
upon calling `start()`.

### `single_thread_context`

Spawns a single background thread that executes tasks scheduled to it.

Call the `.get_scheduler()` method to obtain a scheduler that can be
used to schedule work to this thread.

### `trampoline_scheduler`

An inline scheduler that only allows invoking a maximum number of
operations inline recursively after which time it schedules subsequent
work to run once the call-stack has unwound back to the first call.

### `timed_single_thread_context`

A single-threaded execution context that supports scheduling work at a
particular time via either `schedule_at()` with a time-point or
`schedule_after()` with a delay in addition to the regular `schedule()`
operation which is equivalent to calling `schedule_at()` with the current
time.

Obtain a TimeScheduler by calling the `.get_scheduler()` method.

### `thread_unsafe_event_loop`

An execution context that assumes all accesses to the scheduler are from the same
thread. It does not do any thread-synchronisation internally.

Supports `schedule_at()` and `schedule_after()` operations in addition to
the base `schedule()` operation.

Obtain a TimeScheduler to schedule work onto this context by calling the
`.get_scheduler()` method.

### `new_thread_context`

An execution context that implements the `schedule()` operation by spawning
a new thread to schedule the call to `set_value()`.

If thread creation fails then the `schedule()` operation can fail and `set_error()`
will be called on the receiver inline with the call to `start()`.

The `new_thread_context` keeps track of the threads that have been created
and the destructor will ensure that all of these threads are joined before
returning.

### `linux::io_uring_context`

An I/O event loop execution context that makes use of the Linux io_uring APIs
to perform asynchronous file I/O.

You must call `.run()` from some thread to process tasks and I/O completions
posted to the I/O thread. Only a single call to `.run()` is allowed to execute
at a time.

The `.get_scheduler()` method returns a TimeScheduler object that can be used
to schedule work onto the I/O thread, using the `schedule()` or `schedule_at()`
CPOs.

You can also call one of the following CPOs, passing the scheduler obtained from
a given `io_uring_context`, to open a file:
* `open_file_read_only(scheduler, path) -> AsyncReadFile`
* `open_file_write_only(scheduler, path) -> AsyncWriteFile`
* `open_file_read_write(scheduler, path) -> AsyncReadWriteFile`

You can then use the following CPOs to read from and/or write to that file.
* `async_read_some_at(AsyncReadFile& file, AsyncReadFile::offset_t offset, span<std::byte> buffer)`
* `async_write_some_at(AsyncWriteFile& file, AsyncWriteFile::offset_t offset, span<const std::byte> buffer)`

These CPOs both return a `SenderOf<ssize_t>` that produces the number of bytes written.

For files associated with the `io_uring_context`, these operations will always complete
on the associated on the thread that is calling `run()` on the associated context.

## StopToken Types

### `unstoppable_token`

A trivial stop-token that can never be stopped.

This is used as the default stop-token for the `get_stop_token()`
customisation point.

### `inplace_stop_token` and `inplace_stop_source`

A stop token that can have stop requested via the corresponding
stop-source. The stop-token holds a reference to the stop-source
rather than heap-allocating some shared state. The caller must make
sure that all callbacks are deregistered and that any stop-tokens are
destroyed before the stop-source is destructed.

This is a less-safe but more efficient version of `std::stop_token`
proposed in [P0660R10](https://wg21.link/P0660R10).


## Synchronisation Primitives

### `async_manual_reset_event`

A thread synchronisation event that, when set, must be manually reset.  Waiting
for an event to be set is an (unstoppable) asynchronous operation.

```c++
namespace unifex
{
  struct async_manual_reset_event {
    // Constructs an event in the "unset" state.
    async_manual_reset_event() noexcept;

    // Constructs an event in the "set" state if startSet is true, or the
    // default, "unset" state if startSet is false.
    explicit async_manual_reset_event(bool startSet) noexcept;

    async_manual_reset_event(async_manual_reset_event&&) = delete;
    async_manual_reset_event(const async_manual_reset_event&) = delete;

    ~async_manual_reset_event();

    // Puts the event into the "set" state.  If the event was not already in the
    // "set" state then there may be waiters waiting, in which case they will be
    // resumed.
    //
    // This method has acquire-release semantics.
    void set() noexcept;

    // Returns true iff the event is in the "set" state.
    //
    // This method has acquire semantics.
    bool ready() const noexcept;

    // Puts the event into the "unset" state.
    //
    // This method has acquire-release semantics.
    void reset() noexcept;

    // Returns a sender that will complete when the event is "set".
    //
    // The sender will complete immediately if the event is already "set".
    //
    // Regardless of the receiver to which this sender is connected, the sender
    // is unstoppable.
    //
    // Regardless of whether the sender completes immediately or waits first,
    // the completion will first be scheduled onto the receiver's scheduler with
    // schedule().
    [[nodiscard]] sender auto async_wait() noexcept;
  };
}
```

### `async_mutex`

A mutex that allows acquiring the mutex asynchronously.

```c++
namespace unifex
{
  class async_mutex {
  public:
    async_mutex() noexcept;
    async_mutex(async_mutex&&) = delete;
    async_mutex(const async_mutex&) = delete;
    ~async_mutex();

    // Attempt to acquire the mutex lock synchronously.
    // Returns true if successful, false otherwise.
    // If the lock is acquired then the caller is responsible for releasing
    // the lock by calling unlock().
    bool try_lock() noexcept;

    // Acquire the mutex lock asynchronously.
    // Returns a sender that will complete when the lock has been
    // acquired. The caller is then responsible for calling unlock()
    // to release the mutex.
    sender auto async_lock() noexcept;

    // Unlock the mutex.
    // Only valid to call if you currently own the mutex lock.
    //
    // This will cause the next 'async_lock' operation in the queue to complete
    // (if any).
    void unlock() noexcept;
  };
};
```

## Coroutine support

### `task`

TODO

### `at_coroutine_exit`

`at_coroutine_exit` schedules an asynchronous task to execute when the coroutine exits,
before resuming its parent. The action is guaranteed to execute no matter how the
coroutine exits -- success, failure, or cancel -- like a destructor.

**Usage:**
```c++
auto&& [state...] =
    co_await unifex::at_coroutine_exit(
      [=](auto&&... state) -> task<void> {
        // ... async cleanup action here...
      },
      state...
    );
```

**Arguments:**
The first argument to `at_coroutine_exit` is a callable that returns an awaitable type
(e.g., a `unifex::task<>` or a type that satisfies the `typed_sender` concept).

The other arguments are optional state that may be needed by the cleanup action. The
cleanup action assumes ownership of the passed-in state by copying the state into separate
storage that will persist after the calling coroutine has exited. At that time, rvalue
references to the state will be passed to the callable.

**Returns:**
A tuple of lvalue references to the state owned by the cleanup action.

If you capture references to the state returned by `at_coroutine_exit` (e.g.,
`auto&& [state...] = ...`), then any mutation made to that state will be visible to the
cleanup action when it runs.

**Notes:**
It is possible to register multiple async cleanup actions for the same coroutine by
calling `at_coroutine_exit()` multiple times. Each time it is called, an additional
cleanup action is scheduled. The cleanup actions will execute in reverse order to the
order in which they were registered.

***Caution:***
By the time the cleanup action runs, the coroutine that has scheduled the cleanup action
has already been destroyed, along with any of the coroutine's local variables. Do not
capture references to locals in the cleanup action; those references will dangle. Any
state the cleanup action needs should either be captured by value in the callable or be
passed as arguments to `at_coroutine_exit`, which then owns their lifetime.

For state that is _only_ needed by the cleanup action and _not_ needed by the calling
coroutine, it is sufficient for the callable to capture the state by value rather than
pass the state in as additional arguments.

## Other

### `async_scope`

A place to safely spawn work such that it can be joined later.

```c++
namespace unifex
{
  struct async_scope {
    async_scope() noexcept;
    async_scope(async_scope&&) = delete;
    async_scope(const async_scope&) = delete;

    // Asserts if the sender returned from cleanup has not yet completed.
    ~async_scope();

    // Returns a sender that, when started, marks this scope so that no more
    // work can be spawned within it,
    // requests stop on the internal stop source,
    // i.e. cancellation of all outstanding work, and then waits for all
    // outstanding work to complete.
    //
    // The sender returned from cleanup must complete before this scope is
    // destroyed.
    //
    // cleanup is thread-safe and idempotent (i.e. it can be invoked multiple
    // times in series or in parallel).
    [[nodiscard]] sender auto cleanup() noexcept;

    // Returns a sender that, when started, marks this scope so that no more
    // work can be spawned within it and then waits for all outstanding work
    // to complete.
    //
    // The sender returned from complete must complete before this scope is
    // destroyed.
    //
    // complete is thread-safe and idempotent (i.e. it can be invoked multiple
    // times in series or in parallel).
    [[nodiscard]] sender auto complete() noexcept;

    // Returns a stop token from the scope's internal stop source.
    inplace_stop_token get_stop_token() noexcept;

    // Marks the scope so that no more work can be spawned within it,
    // requests stop on the internal stop source,
    // i.e. cancellation of all outstanding work
    void request_stop() noexcept;

    // Implemented as (void)spawn(sender).
    void detached_spawn(sender);

    // Implemented as detached_spawn(on(scheduler, sender)).
    void detached_spawn_on(scheduler, sender);

    // Implemented as detached_spawn_on(scheduler, just_from(invocable)).
    void detached_spawn_call_on(scheduler, invocable);

    // Connects sender to an internal receiver and starts the operation. Once
    // started, the given sender must complete with void or done; completing
    // with an error will result in a call to std::terminate.
    //
    // The receiver to which the sender is connected responds to get_stop_token
    // with a stoppable token that becomes stopped when clean-up begins.
    //
    // Space for the operation state is allocated with std::make_unique and
    // so this operation may throw if the allocation fails. This operation may
    // also throw if connect throws.
    //
    // Once connect has succeeded, start will only be called if this scope has
    // not yet been cleaned up; if a call to spawn loses a race with a call to
    // cleanup, the operation state created by connect will be destroyed and
    // deallocated without being started.
    //
    // future is a handle to an eagerly-started operation; it is also a Sender,
    // the result is retreived by connecting it to an appropriate Receiver
    // and starting the resulting Operation.
    //
    // Discarding a future without connecting or starting it requests cancellation
    // of the associated operation and discards the operation's result when it
    // ultimately completes.
    //
    // Requesting cancellation of a connected-and-started future will also request
    // cancellation of the associated operation.
    future spawn(sender);

    // Implemented as detached_spawn(on(scheduler, sender)).
    future spawn_on(scheduler, sender);

    // Implemented as spawn_on(scheduler, just_from(invocable)).
    future spawn_call_on(scheduler, invocable);

    // Returns a new sender that, when connected and started, connects and starts
    // the given sender.
    //
    // Returned sender owns a reference to this async_scope. Discarding the sender
    // prior to connecting it or discarding the operation prior to starting it
    // discards the reference to this async_scope.
    //
    // The receiver to which the sender is connected responds to get_stop_token
    // with a stoppable token that becomes stopped when clean-up begins.
    [[nodiscard]] sender auto attach(sender);

    // Implemented as attach(just_from(fun))
    [[nodiscard]] sender auto attach_call(fun);

    // Implemented as attach(on(scheduler, sender))
    [[nodiscard]] sender auto attach_on(scheduler, sender);

    // Implemented as attach_on(scheduler, just_from(fun))
    [[nodiscard]] sender auto attach_call_on(scheduler, fun);
  };
}
```

### `variant_sender`

Non-type erased sender that is parameterized on multiple sender types.
Receivers must implement `set_value` for all value types produced by all
senders.

```
defer(
  [condition]()
      -> variant_sender<decltype(just(42)), decltype(just(true))> {
    if (condition) {
      return just(42);
    } else {
      return just(true);
    }
  });
```
